#include "app.hpp"
#include "anim-sprite.hpp"
#include "bouncer.hpp"
#include "bouncer2.hpp"
#include "channel-dialog.hpp"
#include "chat.hpp"
#include "eye.hpp"
#include "file-open.hpp"
#include "message-dialog.hpp"
#include "mouth.hpp"
#include "preferences-dialog.hpp"
#include "prj-dialog.hpp"
#include "root.hpp"
#include "ui.hpp"
#include <SDL_opengl.h>
#include <fstream>
#include <log/log.hpp>

static auto getProjMat() -> glm::mat4
{
  GLfloat projMatData[16];
  glGetFloatv(GL_PROJECTION_MATRIX, projMatData);
  return glm::make_mat4(projMatData);
}

App::App()
  : audioInput(preferences.inputAudio, wav2Visemes.sampleRate(), wav2Visemes.frameSize()),
    lib(preferences)
{
  LOG("sample rate:", wav2Visemes.sampleRate());
  LOG("frame size:", wav2Visemes.frameSize());
  audioInput.reg(wav2Visemes);
  saveFactory.reg<Bouncer>(
    [this](std::string) { return std::make_unique<Bouncer>(lib, undo, audioInput); });
  saveFactory.reg<Bouncer2>([this](std::string name) {
    return std::make_unique<Bouncer2>(lib, undo, audioInput, std::move(name));
  });
  saveFactory.reg<Root>([this](std::string) { return std::make_unique<Root>(lib, undo); });
  saveFactory.reg<Mouth>([this](std::string name) {
    return std::make_unique<Mouth>(wav2Visemes, lib, undo, std::move(name));
  });
  saveFactory.reg<AnimSprite>(
    [this](std::string name) { return std::make_unique<AnimSprite>(lib, undo, std::move(name)); });
  saveFactory.reg<Eye>([this](std::string name) {
    return std::make_unique<Eye>(mouseTracking, lib, undo, std::move(name));
  });
  saveFactory.reg<Chat>(
    [this](std::string name) { return std::make_unique<Chat>(lib, undo, uv, std::move(name)); });
}

auto App::render(float dt) -> void
{
  if (!root)
  {
    glClearColor(0x45 / 255.f, 0x44 / 255.f, 0x7d / 255.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    return;
  }

  if (showUi && !isMinimized)
    root->renderAll(dt, hovered, selected);
  else
    root->renderAll(dt, nullptr, nullptr);
}

auto App::renderUi(float /*dt*/) -> void
{
  if (!root)
  {
    if (!dialog)
      dialog = std::make_unique<PrjDialog>(lib, [this](bool) { loadPrj(); });
    if (!dialog->draw())
      dialog = nullptr;
    return;
  }

  ImGuiIO &io = ImGui::GetIO();
  ImGuiStyle &style = ImGui::GetStyle();
  if (isMinimized)
    return;
  if (!showUi)
  {
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
      style.Colors[ImGuiCol_WindowBg].w = .2f;
    auto showUiWindow = Ui::Window("##Show UI");
    if (ImGui::Button("Show UI"))
      showUi = true;
    return;
  }

  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    style.Colors[ImGuiCol_WindowBg].w = .8f;
  if (auto mainMenu = Ui::MainMenuBar{})
  {
    if (auto fileMenu = Ui::Menu{"File"})
      if (ImGui::MenuItem("Save"))
        savePrj();
    if (auto editMenu = Ui::Menu{"Edit"})
    {
      {
        auto undoDisabled = Ui::Disabled(!undo.hasUndo());
        if (auto undoMenu = ImGui::MenuItem("Undo", "CTRL+Z"))
          undo.undo();
      }
      {
        auto redoDisabled = Ui::Disabled(!undo.hasRedo());
        if (auto redoMenu = ImGui::MenuItem("Redo", "CTRL+Y"))
          undo.redo();
      }
      if (auto addMenu = Ui::Menu{"Add"})
      {
        if (ImGui::MenuItem("Mouth..."))
          dialog =
            std::make_unique<FileOpen>(lib, "Add Mouth Dialog", [this](bool r, const auto &filePath) {
              if (r)
                addNode(Mouth::className, filePath.string());
            });
        if (ImGui::MenuItem("Eye..."))
          dialog =
            std::make_unique<FileOpen>(lib, "Add Eye Dialog", [this](bool r, const auto &filePath) {
              if (r)
                addNode(Eye::className, filePath.string());
            });

        if (ImGui::MenuItem("Sprite..."))
          dialog =
            std::make_unique<FileOpen>(lib, "Add Sprite Dialog", [this](bool r, const auto &filePath) {
              if (r)
                addNode(AnimSprite::className, filePath.string());
            });

        if (ImGui::MenuItem("Twitch Chat..."))
          dialog = std::make_unique<ChannelDialog>("mika314", [this](bool r, const auto &channel) {
            if (r)
              addNode(Chat::className, channel);
          });
        if (ImGui::MenuItem("Bouncer"))
          addNode(Bouncer2::className, "bouncer");
      }
      if (ImGui::MenuItem("Preferences..."))
        dialog =
          std::make_unique<PreferencesDialog>(preferences, audioOutput, audioInput, [this](bool r) {
            if (r)
              lib.flush();
          });
    }
  }

  if (dialog)
    if (!dialog->draw())
      dialog = nullptr;
  {
    auto outlinerWindow = Ui::Window("Outliner");
    {
      auto hierarchyButtonsDisabled = Ui::Disabled(!selected);
      if (ImGui::Button("<"))
        selected->unparent();
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Unparent");
      ImGui::SameLine();
      if (ImGui::Button("^"))
        selected->moveUp();
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Move up");
      ImGui::SameLine();
      if (ImGui::Button("V"))
        selected->moveDown();
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Move down");
      ImGui::SameLine();
      if (ImGui::Button(">"))
        selected->parentWithBellow();
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Parent with below");
    }
    renderTree(*root);
    ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
    if (ImGui::Button("Hide UI"))
      showUi = false;
  }
  {
    auto detailsWindpw = Ui::Window("Details");
    if (selected)
      if (auto detailsTable = Ui::Table{"Details", 2, ImGuiTableFlags_SizingStretchProp})
      {
        ImGui::TableSetupColumn("Property     ", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        selected->renderUi();
      }
  }
  for (auto &action : postponedActions)
    action();
  postponedActions.clear();
}

auto App::processIo() -> void
{
  if (!root)
    return;
  // Check if ImGui did not process any user input
  ImGuiIO &io = ImGui::GetIO();
  if (!io.WantCaptureMouse)
  {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
      if (!selected || selected->editMode() == Node::EditMode::select)
      {
        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);
        const auto projMat = getProjMat();
        auto newSelected = root->nodeUnder(projMat, glm::vec2{1.f * mouseX, 1.f * mouseY});
        if (newSelected != selected)
          undo.record([newSelected, this]() { selected = newSelected; },
                      [oldSelected = selected, this]() { selected = oldSelected; });
      }
      else
      {
        if (selected)
          selected->commit();
      }
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
      cancel();
  }
  if (!io.WantCaptureKeyboard || !io.WantCaptureMouse)
  {
    if (selected && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper)
    {
      if (ImGui::IsKeyPressed(ImGuiKey_G))
      {
        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);
        selected->translateStart(glm::vec2{1.f * mouseX, 1.f * mouseY});
      }

      if (ImGui::IsKeyPressed(ImGuiKey_S))
      {
        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);
        selected->scaleStart(glm::vec2{1.f * mouseX, 1.f * mouseY});
      }

      if (ImGui::IsKeyPressed(ImGuiKey_R))
      {
        int mouseX, mouseY;
        SDL_GetMouseState(&mouseX, &mouseY);
        selected->rotStart(glm::vec2{1.f * mouseX, 1.f * mouseY});
      }
      if (ImGui::IsKeyPressed(ImGuiKey_X) || ImGui::IsKeyPressed(ImGuiKey_Delete))
        Node::del(&selected);
      if (ImGui::IsKeyPressed(ImGuiKey_D))
      {
        if (selected)
        {
          OStrm os;
          selected->saveAll(os);
          const auto s = os.str();
          IStrm is(s.data(), s.data() + s.size());
          std::string className;
          std::string name;
          ::deser(is, className);
          ::deser(is, name);
          LOG(className, name);
          auto n = std::shared_ptr{saveFactory.ctor(className, name)};
          n->loadAll(saveFactory, is);
          undo.record(
            [n, parent = selected->parent(), this]() {
              selected = n.get();
              parent->addChild(std::move(n));
            },
            [n, oldSelected = selected, this]() {
              Node::del(*n);
              selected = oldSelected;
            });
          int mouseX, mouseY;
          SDL_GetMouseState(&mouseX, &mouseY);
          selected->translateStart(glm::vec2{1.f * mouseX, 1.f * mouseY});
        }
      }
      if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        cancel();
    }
  }
  if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper && ImGui::IsKeyPressed(ImGuiKey_Z))
    undo.undo();
  if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper && ImGui::IsKeyPressed(ImGuiKey_Y))
    undo.redo();
}

auto App::cancel() -> void
{
  if (!selected)
    return;

  selected->cancel();
}

auto App::tick(float /*dt*/) -> void
{
  audioInput.tick();
  uv.tick();

  if (!root)
    return;

  const auto projMat = getProjMat();
  int mouseX, mouseY;
  SDL_GetMouseState(&mouseX, &mouseY);
  hovered = nullptr;
  const auto mousePos = glm::vec2{1.f * mouseX, 1.f * mouseY};
  if (!selected || selected->editMode() == Node::EditMode::select)
    hovered = root->nodeUnder(projMat, mousePos);
  else
  {
    if (selected)
      selected->update(projMat, mousePos);
  }
  mouseTracking.tick();
}

auto App::renderTree(Node &v) -> void
{
  ImGuiTreeNodeFlags baseFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                 ImGuiTreeNodeFlags_SpanAvailWidth;
  ImGuiTreeNodeFlags nodeFlags = baseFlags;

  const auto nm = v.getName();
  if (selected == &v)
    nodeFlags |= ImGuiTreeNodeFlags_Selected;
  const auto &nodes = v.getNodes();
  if (!nodes.empty())
  {
    nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
    const auto nodeOpen = ImGui::TreeNodeEx(&v, nodeFlags, "%s", nm.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
      undo.record([&v, this]() { selected = &v; },
                  [oldSelected = selected, this]() { selected = oldSelected; });
    if (nodeOpen)
    {
      for (const auto &n : nodes)
        renderTree(*n);
      ImGui::TreePop();
    }
  }
  else
  {
    nodeFlags |=
      ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen; // ImGuiTreeNodeFlags_Bullet
    ImGui::TreeNodeEx(&v, nodeFlags, "%s", nm.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
      undo.record([&v, this]() { selected = &v; },
                  [oldSelected = selected, this]() { selected = oldSelected; });
  }
}

static const auto ver = uint32_t{2};

auto App::loadPrj() -> void
{
  ImGui::LoadIniSettingsFromDisk("imgui.ini");

  std::ifstream st("prj.tpp", std::ofstream::binary);
  if (!st)
  {
    root = std::make_unique<Root>(lib, undo);
    LOG("Create new project");
    return;
  }

  std::ostringstream buffer;
  buffer << st.rdbuf();

  auto buf = buffer.str();

  IStrm strm(buf.data(), buf.data() + buf.size());

  uint32_t v;
  ::deser(strm, v);
  if (v != ver)
  {
    root = std::make_unique<Root>(lib, undo);
    LOG("Version mismatch expected:", ver, ", received:", v);
    return;
  }

  std::string className;
  std::string name;
  ::deser(strm, className);
  ::deser(strm, name);
  LOG(className, name);
  root = saveFactory.ctor(className, name);
  root->loadAll(saveFactory, strm);
}

auto App::savePrj() -> void
{
  if (!root)
    return;
  OStrm strm;
  ::ser(strm, ver);
  root->saveAll(strm);
  std::ofstream st("prj.tpp", std::ofstream::binary);
  st.write(strm.str().data(), strm.str().size());
}

auto App::addNode(const std::string &class_, const std::string &name) -> void
{
  try
  {
    auto node = std::shared_ptr{saveFactory.ctor(class_, name)};
    auto oldSelected = selected;
    undo.record(
      [node, parent = (oldSelected ? oldSelected : root.get()), this]() {
        selected = node.get();
        parent->addChild(std::move(node));
      },
      [node, oldSelected, this]() {
        Node::del(*selected);
        selected = oldSelected;
      });
  }
  catch (const std::runtime_error &e)
  {
    postponedActions.emplace_back(
      [&]() { dialog = std::make_unique<MessageDialog>("Error", e.what()); });
    LOG(e.what());
  }
};
