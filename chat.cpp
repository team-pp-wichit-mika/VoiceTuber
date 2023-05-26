#include "chat.hpp"
#include "lib.hpp"
#include "ui.hpp"
#include "undo.hpp"
#include <log/log.hpp>
#include <sstream>

Chat::Chat(class Lib &aLib,
           Undo &aUndo,
           class Uv &aUv,
           HttpClient &aHttpClient,
           AudioSink &aAudioSink,
           std::string n)
  : Node(aLib, aUndo, n),
    lib(aLib),
    uv(aUv),
    httpClient(aHttpClient),
    audioSink(aAudioSink),
    twitch(aLib.queryTwitch(aUv, n)),
    font(aLib.queryFont(SDL_GetBasePath() + std::string{"assets/notepad_font/NotepadFont.ttf"}, ptsize)),
    timer(aUv.getTimer())
{
  twitch->reg(*this);
}

Chat::~Chat()
{
  twitch->unreg(*this);
}

static auto escName(std::string value) -> std::string
{
  std::transform(std::begin(value), std::end(value), std::begin(value), [](char ch) {
    if (ch == '_')
      return ' ';
    return ch;
  });
  while (!value.empty() && isdigit(value.back()))
    value.resize(value.size() - 1);
  return value;
}

static auto eq(const std::vector<std::string> &words, size_t i, size_t j, size_t w)
{
  if (i + w > words.size())
    return false;
  if (j + w > words.size())
    return false;
  for (auto k = 0U; k < w; ++k)
    if (words[i + k] != words[j + k])
      return false;
  return true;
}

static auto dedup(const std::string &var)
{
  std::vector<std::string> words;
  std::string word;
  std::istringstream st(var);
  while (std::getline(st, word, ' '))
    words.push_back(word);
  for (bool didUpdate = true; didUpdate;)
  {
    didUpdate = false;
    for (auto w = 1U; w < words.size() / 2 && !didUpdate; ++w)
      for (auto i = 0U; i < words.size() - w && !didUpdate; ++i)
        for (auto r = 1U; !didUpdate; ++r)
          if (!eq(words, i, i + r * w, w))
          {
            if (r >= 3)
            {
              words.erase(std::begin(words) + i + w, std::begin(words) + i + r * w);
              didUpdate = true;
            }
            else
              break;
          }
  }
  std::string ret;
  for (const auto &w : words)
  {
    if (!ret.empty())
      ret += " ";
    ret += w;
  }
  return ret;
}

static auto getDialogLine(const std::string &text, bool isMe)
{
  if (isMe)
    return "";
  if (text.find("?") != std::string::npos || text.find("!") == 0)
    return "asked:";
  if (text.find("!") != std::string::npos)
    return "yelled:";
  return "said:";
}

auto Chat::onMsg(Msg val) -> void
{
  showChat = true;
  timer.stop();
  timer.start([this]() { showChat = false; }, 30'000, false /*repeat*/);
  if (azureTts)
  {
    const auto name = val.displayName;
    const auto text = val.msg;
    const auto isMe = false; // val.isMe;
    const auto supressName = (lastName == name) && !isMe;
    azureTts->say(getVoice(name),
                  (!supressName ? (escName(name) + " " + getDialogLine(text, isMe) + " ") : "") +
                    dedup(text));
    lastName = name;
  }
  msgs.emplace_back(std::move(val));
}

auto Chat::getVoice(const std::string &n) const -> std::string
{
  auto it = voicesMap.find(n);
  if (it != std::end(voicesMap))
    return it->second;
  return voices[(std::hash<std::string>()(n) ^ 1) % voices.size()];
}

auto Chat::save(OStrm &strm) const -> void
{
  ::ser(strm, className);
  ::ser(strm, name);
  ::ser(strm, *this);
  Node::save(strm);
}

auto Chat::load(IStrm &strm) -> void
{
  ::deser(strm, *this);
  Node::load(strm);
  font =
    lib.get().queryFont(SDL_GetBasePath() + std::string{"assets/notepad_font/NotepadFont.ttf"}, ptsize);
  if (tts)
  {
    if (!azureTts)
    {
      azureTts = lib.get().queryAzureTts(uv, httpClient, audioSink);
      azureTts->listVoices([this](std::vector<std::string> aVoices) { voices = std::move(aVoices); });
    }
  }
}

auto Chat::render(float dt, Node *hovered, Node *selected) -> void
{
  if (!showChat)
  {
    Node::render(dt, hovered, selected);
    return;
  }
  auto y = 0.f;
  for (auto it = msgs.rbegin(); it != msgs.rend(); ++it)
  {
    const auto displayNameDim = font->getSize(it->displayName);
    auto msg = std::ostringstream{};
    msg << ": " << it->msg;

    glColor3f(1.f, 1.f, 1.f);
    const auto wrappedLines = wrapText(msg.str(), displayNameDim.x);
    for (auto ln = wrappedLines.rbegin(); ln != wrappedLines.rend(); ++ln)
    {
      if (y > h())
        break;
      const auto isLast = ln == (wrappedLines.rend() - 1);
      font->render(glm::vec2{isLast ? displayNameDim.x : 0, y}, *ln);
      if (isLast)
      {
        glColor3f(it->color.x, it->color.y, it->color.z);
        font->render(glm::vec2{0.f, y}, it->displayName);
      }
      y += displayNameDim.y;
    }

    if (y > h())
      break;
  }

  Node::render(dt, hovered, selected);
}

auto Chat::wrapText(const std::string &text, float initOffset) const -> std::vector<std::string>
{
  std::vector<std::string> lines;
  auto iss = std::istringstream{text};
  std::string word;
  iss >> word;
  auto line = std::move(word);
  while (iss >> word)
  {
    auto tempLine = line + " " + word;
    const auto tempDim = font->getSize(tempLine);
    if (tempDim.x > w() - initOffset)
    {
      initOffset = 0;
      lines.emplace_back(std::move(line));
      line = word;
      continue;
    }
    line = std::move(tempLine);
  }
  if (!line.empty())
    lines.emplace_back(std::move(line));

  return lines;
}

auto Chat::renderUi() -> void
{
  Node::renderUi();
  ImGui::TableNextColumn();
  Ui::textRj("Size");
  ImGui::TableNextColumn();
  Ui::dragFloat(undo,
                "##width",
                size.x,
                1.f,
                -std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                "%.1f");
  Ui::dragFloat(undo,
                "##Height",
                size.y,
                1.f,
                -std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                "%.1f");

  ImGui::TableNextColumn();
  Ui::textRj("Font Size");
  ImGui::TableNextColumn();
  const auto oldSize = ptsize;
  if (ImGui::InputInt("##Font Size", &ptsize))
    undo.get().record(
      [newSize = ptsize, this]() {
        ptsize = newSize;
        font = lib.get().queryFont(
          SDL_GetBasePath() + std::string{"assets/notepad_font/NotepadFont.ttf"}, ptsize);
      },
      [oldSize, this]() {
        ptsize = oldSize;
        font = lib.get().queryFont(
          SDL_GetBasePath() + std::string{"assets/notepad_font/NotepadFont.ttf"}, ptsize);
      });
  ImGui::TableNextColumn();
  Ui::textRj("Azure TTS");
  ImGui::TableNextColumn();
  {
    auto oldTts = tts;
    tts = static_cast<bool>(azureTts);
    if (ImGui::Checkbox("##AzureTTS", &tts))
    {
      undo.get().record(
        [this, newTts = tts]() {
          if (newTts)
          {
            if (!azureTts)
            {
              azureTts = lib.get().queryAzureTts(uv, httpClient, audioSink);
              azureTts->listVoices(
                [this](std::vector<std::string> aVoices) { voices = std::move(aVoices); });
            }
          }
          else
            azureTts = nullptr;
        },
        [this, oldTts]() {
          if (oldTts)
          {
            if (!azureTts)
            {
              azureTts = lib.get().queryAzureTts(uv, httpClient, audioSink);
              azureTts->listVoices(
                [this](std::vector<std::string> aVoices) { voices = std::move(aVoices); });
            }
          }
          else
            azureTts = nullptr;
        });
    }
  }
  ImGui::TableNextColumn();
  ImGui::Text("Voices Mapping");
  ImGui::TableNextColumn();

  for (const auto &v : voicesMap)
  {
    ImGui::TableNextColumn();
    Ui::textRj(v.first.c_str());
    ImGui::TableNextColumn();
    ImGui::Text("%s", v.second.c_str());
  }
  {
    ImGui::TableNextColumn();
    char chatterNameBuf[1024];
    strcpy(chatterNameBuf, chatterName.data());
    if (ImGui::InputText("##Chatter Name", chatterNameBuf, sizeof(chatterNameBuf)))
      chatterName = chatterNameBuf;
    ImGui::TableNextColumn();
    {
      auto combo = Ui::Combo("##Chatter Voice", chatterVoice.c_str(), 0);
      if (combo)
        for (const auto &voice : voices)
          if (ImGui::Selectable((voice + std::string{"##Voice"}).c_str(), chatterVoice == voice))
            chatterVoice = voice;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add##AddVoiceMap"))
    {
      if (!chatterVoice.empty())
        voicesMap[chatterName] = chatterVoice;
      else
        voicesMap.erase(chatterName);
    }
    ImGui::SameLine();
    if (ImGui::Button("Del##AddVoiceMap"))
      voicesMap.erase(chatterName);
  }

  if (!twitch->isConnected())
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 0.7f, 0.7f, 1.0f));

  ImGui::TableNextColumn();
  Ui::textRj("Chat");
  ImGui::TableNextColumn();
  if (auto chatListBox =
        Ui::ListBox{"##Chat", ImVec2(-FLT_MIN, 5 * ImGui::GetTextLineHeightWithSpacing())})
    for (const auto &msg : msgs)
      ImGui::Text("%s: %s", msg.displayName.c_str(), msg.msg.c_str());
  if (!twitch->isConnected())
    ImGui::PopStyleColor();
}

auto Chat::h() const -> float
{
  return size.y;
}

auto Chat::w() const -> float
{
  return size.x;
}
