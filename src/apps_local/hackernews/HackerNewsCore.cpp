#include "HackerNewsCore.h"

#include <Utf8.h>

#include <cctype>
#include <cstdlib>

namespace hn {
namespace {

bool isBlank(const std::string_view line) {
  for (const char c : line) {
    if (std::isspace(static_cast<unsigned char>(c)) == 0) return false;
  }
  return true;
}

std::string trimmed(const std::string_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
  return std::string(text.substr(begin, end - begin));
}

bool startsWith(const std::string_view text, const std::string_view prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

// Case-insensitive, because a URL's host and path arrive however the poster
// typed them and ".PDF" is still a PDF.
bool containsFold(const std::string_view haystack, const std::string_view needle) {
  if (needle.empty() || haystack.size() < needle.size()) return false;
  for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
    size_t j = 0;
    while (j < needle.size() && std::tolower(static_cast<unsigned char>(haystack[i + j])) ==
                                    std::tolower(static_cast<unsigned char>(needle[j]))) {
      ++j;
    }
    if (j == needle.size()) return true;
  }
  return false;
}

// The path, without query or fragment, so "?ref=x.pdf" cannot be mistaken for a
// PDF and "/paper.pdf?dl=1" still is one.
std::string_view urlPath(std::string_view url) {
  const size_t scheme = url.find("://");
  if (scheme != std::string_view::npos) url.remove_prefix(scheme + 3);
  const size_t cut = url.find_first_of("?#");
  if (cut != std::string_view::npos) url = url.substr(0, cut);
  return url;
}

bool endsWithFold(const std::string_view text, const std::string_view suffix) {
  if (text.size() < suffix.size()) return false;
  return containsFold(text.substr(text.size() - suffix.size()), suffix);
}

void appendUtf8(std::string& out, const uint32_t codepoint) {
  if (codepoint < 0x80) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

// Markdown link targets removed, labels kept. Used by both the gate and the
// flattener, so a paragraph is measured as the same text it will be drawn as.
std::string delinked(const std::string_view markdown) {
  std::string out;
  out.reserve(markdown.size());
  size_t i = 0;
  while (i < markdown.size()) {
    const bool image = markdown[i] == '!' && i + 1 < markdown.size() && markdown[i + 1] == '[';
    if (markdown[i] == '[' || image) {
      const size_t open = image ? i + 1 : i;
      const size_t close = markdown.find(']', open);
      if (close != std::string_view::npos && close + 1 < markdown.size() && markdown[close + 1] == '(') {
        const size_t target = markdown.find(')', close + 2);
        if (target != std::string_view::npos) {
          // An image contributes nothing a reader can use; a link contributes
          // its label.
          if (!image) out.append(markdown.substr(open + 1, close - open - 1));
          i = target + 1;
          continue;
        }
      }
    }
    out.push_back(markdown[i]);
    ++i;
  }
  return out;
}

// Blocks separated by one or more blank lines, each trimmed, empties dropped.
std::vector<std::string> blocks(const std::string_view text) {
  std::vector<std::string> out;
  std::string current;
  size_t i = 0;
  while (i <= text.size()) {
    const size_t nl = text.find('\n', i);
    const std::string_view line = text.substr(i, (nl == std::string_view::npos ? text.size() : nl) - i);
    if (isBlank(line)) {
      const std::string block = trimmed(current);
      if (!block.empty()) out.push_back(block);
      current.clear();
    } else {
      if (!current.empty()) current.push_back('\n');
      current.append(line);
    }
    if (nl == std::string_view::npos) break;
    i = nl + 1;
  }
  const std::string block = trimmed(current);
  if (!block.empty()) out.push_back(block);
  return out;
}

// A block that is a list, heading, table row or rule is structure rather than
// prose. Anything else long enough is somebody talking.
bool readsAsParagraph(const std::string& block) {
  if (static_cast<int>(block.size()) < kProseBlockMin) return false;
  const char first = block[0];
  return first != '*' && first != '-' && first != '|' && first != '#';
}

}  // namespace

// --- The readability gate ------------------------------------------------

int proseChars(const std::string_view markdown) {
  int total = 0;
  for (const std::string& block : blocks(delinked(markdown))) {
    if (readsAsParagraph(block)) total += static_cast<int>(block.size());
  }
  return total;
}

bool urlCanBeArticle(const std::string_view url) {
  if (url.empty()) return false;

  const std::string_view path = urlPath(url);

  // Documents and binaries the extractor answers with an empty body. The PDF on
  // the front page the day this was written scored exactly 0.
  static constexpr std::string_view kSuffixes[] = {".pdf", ".zip", ".tar",  ".gz",  ".mp3", ".mp4",
                                                   ".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp"};
  for (const std::string_view suffix : kSuffixes) {
    if (endsWithFold(path, suffix)) return false;
  }

  // Hosts whose content is not a page of prose, or is only reachable to a
  // browser running their JavaScript. Twitter extracts to "Something went
  // wrong" and nothing else, every time.
  static constexpr std::string_view kHosts[] = {"twitter.com", "x.com/",        "youtube.com", "youtu.be",
                                                "reddit.com",  "instagram.com", "tiktok.com",  "news.ycombinator.com"};
  for (const std::string_view host : kHosts) {
    if (containsFold(path, host)) return false;
  }
  return true;
}

// --- Hacker News's own text ----------------------------------------------

void decodeEntities(std::string& text) {
  std::string out;
  out.reserve(text.size());
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] != '&') {
      out.push_back(text[i++]);
      continue;
    }
    const size_t semi = text.find(';', i + 1);
    // A bare ampersand, or one too far from its semicolon to be an entity.
    if (semi == std::string::npos || semi - i > 10) {
      out.push_back(text[i++]);
      continue;
    }
    const std::string_view body(text.data() + i + 1, semi - i - 1);
    if (!body.empty() && body[0] == '#') {
      const bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
      const std::string digits(body.substr(hex ? 2 : 1));
      char* end = nullptr;
      const unsigned long value = std::strtoul(digits.c_str(), &end, hex ? 16 : 10);
      if (end != nullptr && *end == '\0' && !digits.empty() && value != 0 && value <= 0x10FFFF) {
        appendUtf8(out, static_cast<uint32_t>(value));
        i = semi + 1;
        continue;
      }
    } else if (body == "amp") {
      out.push_back('&');
      i = semi + 1;
      continue;
    } else if (body == "lt") {
      out.push_back('<');
      i = semi + 1;
      continue;
    } else if (body == "gt") {
      out.push_back('>');
      i = semi + 1;
      continue;
    } else if (body == "quot") {
      out.push_back('"');
      i = semi + 1;
      continue;
    } else if (body == "apos") {
      out.push_back('\'');
      i = semi + 1;
      continue;
    } else if (body == "nbsp") {
      out.push_back(' ');
      i = semi + 1;
      continue;
    }
    // Not an entity we know: leave it exactly as written.
    out.push_back(text[i++]);
  }
  // And fold what the decoding just produced. HN writes its apostrophes and
  // quotes as numeric entities, so &#8217; and &#x201C; arrive here as ASCII
  // and leave as U+2019 and U+201C -- codepoints the reading cut has no glyph
  // for, which draw as NOTHING. This function is where they come into
  // existence, so it is where they go back out. See lib/Utf8/Utf8.h.
  text = utf8FoldTypography(out);
}

std::vector<std::string> paragraphsFromHnHtml(const std::string_view html) {
  std::vector<std::string> out;
  std::string current;

  const auto flush = [&out, &current]() {
    decodeEntities(current);
    const std::string paragraph = trimmed(current);
    if (!paragraph.empty()) out.push_back(paragraph);
    current.clear();
  };

  size_t i = 0;
  while (i < html.size()) {
    if (html[i] != '<') {
      current.push_back(html[i++]);
      continue;
    }
    const size_t close = html.find('>', i + 1);
    if (close == std::string_view::npos) {
      // An unclosed angle bracket is a character somebody typed.
      current.push_back(html[i++]);
      continue;
    }
    std::string name;
    for (size_t j = i + 1; j < close; ++j) {
      const char c = html[j];
      if (std::isspace(static_cast<unsigned char>(c)) != 0) break;
      name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    // HN separates paragraphs with an opening <p> and never closes it.
    if (name == "p" || name == "/p" || name == "br" || name == "br/") {
      flush();
    }
    // Every other tag in HN's vocabulary carries no meaning this panel can
    // draw, so it contributes nothing and its content flows on.
    i = close + 1;
  }
  flush();
  return out;
}

// --- The extractor's answer ----------------------------------------------

Extracted splitExtractorResponse(const std::string_view response) {
  Extracted result;
  static constexpr std::string_view kMarker = "Markdown Content:";
  static constexpr std::string_view kTitle = "Title:";

  const size_t marker = response.find(kMarker);
  const std::string_view header = marker == std::string_view::npos ? response : response.substr(0, marker);
  result.body =
      marker == std::string_view::npos ? std::string(response) : std::string(response.substr(marker + kMarker.size()));

  size_t i = 0;
  while (i < header.size()) {
    const size_t nl = header.find('\n', i);
    const std::string_view line = header.substr(i, (nl == std::string_view::npos ? header.size() : nl) - i);
    if (startsWith(line, kTitle)) {
      result.title = utf8FoldTypography(trimmed(line.substr(kTitle.size())));
      break;
    }
    if (nl == std::string_view::npos) break;
    i = nl + 1;
  }
  return result;
}

std::vector<std::string> paragraphsFromMarkdown(const std::string_view markdown) {
  std::vector<std::string> out;
  for (const std::string& block : blocks(delinked(markdown))) {
    std::string flat;
    flat.reserve(block.size());

    bool atLineStart = true;
    size_t i = 0;
    while (i < block.size()) {
      const char c = block[i];

      if (atLineStart) {
        // Leading structure: heading hashes, quote markers, list bullets.
        size_t j = i;
        while (j < block.size() && (block[j] == ' ' || block[j] == '\t')) ++j;
        size_t k = j;
        while (k < block.size() && block[k] == '#') ++k;
        if (k > j && k < block.size() && block[k] == ' ') {
          i = k + 1;
          continue;
        }
        if (block[j] == '>') {
          i = j + 1;
          while (i < block.size() && block[i] == ' ') ++i;
          continue;
        }
        if ((block[j] == '*' || block[j] == '-' || block[j] == '+') && j + 1 < block.size() && block[j + 1] == ' ') {
          flat.append("- ");
          i = j + 2;
          atLineStart = false;
          continue;
        }
        atLineStart = false;
      }

      // A fence or a rule contributes nothing; its content is already text.
      if (c == '`') {
        while (i < block.size() && block[i] == '`') ++i;
        continue;
      }
      if (c == '*' || c == '_') {
        size_t run = 0;
        while (i + run < block.size() && block[i + run] == c) ++run;
        // A run of three or more on its own is a horizontal rule; one or two
        // wrapping words is emphasis. Either way the marker is not a word.
        i += run;
        continue;
      }
      if (c == '\n') {
        // A block is one paragraph, so its internal wrapping becomes spaces.
        if (!flat.empty() && flat.back() != ' ') flat.push_back(' ');
        atLineStart = true;
        ++i;
        continue;
      }
      flat.push_back(c);
      ++i;
    }

    // The extractor's Markdown comes from somebody else's page rather than from
    // HN, so it carries real curly quotes and em dashes rather than entities.
    const std::string paragraph = utf8FoldTypography(trimmed(flat));
    if (!paragraph.empty()) out.push_back(paragraph);
  }
  return out;
}

// --- Reading a comment tree without holding it ---------------------------

CommentScanner::CommentScanner(std::vector<Comment>& out, const Limits& limits) : out_(out), limits_(limits) {}

CommentScanner::Frame* CommentScanner::enclosingObject() {
  for (size_t i = depth_; i > 0; --i) {
    if (stack_[i - 1].kind == Container::Object) return &stack_[i - 1];
  }
  return nullptr;
}

bool CommentScanner::push(const Container kind) {
  if (depth_ >= kMaxNesting) {
    failed_ = true;
    return false;
  }
  Frame frame;
  frame.kind = kind;

  if (kind == Container::ChildrenArray) ++childDepth_;

  // An object opened directly inside a children array is a comment, and this is
  // where it claims its place in the output. Everything about it arrives later.
  if (kind == Container::Object && depth_ > 0 && stack_[depth_ - 1].kind == Container::ChildrenArray) {
    ++seen_;
    const bool room = static_cast<int>(out_.size()) < limits_.maxComments && kept_ < limits_.maxTextBytes;
    if (room) {
      Comment comment;
      comment.depth = childDepth_ - 1 < kMaxCommentDepth ? childDepth_ - 1 : kMaxCommentDepth;
      out_.push_back(std::move(comment));
      frame.slot = static_cast<int>(out_.size()) - 1;
    } else {
      truncated_ = true;
    }
  }

  stack_[depth_++] = frame;
  return true;
}

void CommentScanner::pop() {
  if (depth_ == 0) {
    failed_ = true;
    return;
  }
  --depth_;
  if (stack_[depth_].kind == Container::ChildrenArray && childDepth_ > 0) --childDepth_;
}

// A completed string value, assigned to whatever key preceded it.
void CommentScanner::onValue() {
  Frame* owner = enclosingObject();
  if (owner == nullptr || owner->slot < 0) return;
  Comment& comment = out_[static_cast<size_t>(owner->slot)];

  if (key_ == "author") {
    comment.author = utf8FoldTypography(buffer_);
  } else if (key_ == "text") {
    if (buffer_.size() > limits_.maxCommentBytes) buffer_.resize(limits_.maxCommentBytes);
    comment.paragraphs = paragraphsFromHnHtml(buffer_);
    kept_ += buffer_.size();
    if (kept_ >= limits_.maxTextBytes) truncated_ = true;
  }
}

bool CommentScanner::feed(const char* data, const size_t length) {
  if (failed_) return false;

  for (size_t i = 0; i < length; ++i) {
    const char c = data[i];

    if (inString_) {
      if (escaped_) {
        escaped_ = false;
        switch (c) {
          case 'n':
            buffer_.push_back('\n');
            break;
          case 't':
            buffer_.push_back('\t');
            break;
          case 'r':
            break;  // HN's text uses \n alone; a stray CR is not a character
          case 'b':
          case 'f':
            break;
          case 'u': {
            // \uXXXX. The four digits may straddle a chunk boundary, so they
            // are gathered through the same buffer rather than read ahead.
            escaped_ = false;
            unicodePending_ = 4;
            unicode_ = 0;
            break;
          }
          default:
            buffer_.push_back(c);
            break;  // \" \\ \/
        }
        continue;
      }
      if (unicodePending_ > 0) {
        unsigned digit = 0;
        if (c >= '0' && c <= '9') {
          digit = static_cast<unsigned>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
          digit = static_cast<unsigned>(c - 'a') + 10u;
        } else if (c >= 'A' && c <= 'F') {
          digit = static_cast<unsigned>(c - 'A') + 10u;
        }
        unicode_ = (unicode_ << 4) | digit;
        if (--unicodePending_ == 0) {
          // Surrogate halves are dropped rather than paired: HN's text is
          // overwhelmingly BMP, and a lone half is not a character either way.
          if (unicode_ < 0xD800 || unicode_ > 0xDFFF) appendUtf8(buffer_, unicode_);
        }
        continue;
      }
      if (c == '\\') {
        escaped_ = true;
        continue;
      }
      if (c == '"') {
        inString_ = false;
        if (isKey_) {
          key_ = buffer_;
        } else {
          onValue();
          key_.clear();
        }
        buffer_.clear();
        continue;
      }
      buffer_.push_back(c);
      continue;
    }

    switch (c) {
      case '"':
        inString_ = true;
        buffer_.clear();
        // A string is a key unless a colon has just promised a value.
        isKey_ = !wantValue_;
        wantValue_ = false;
        break;
      case ':':
        wantValue_ = true;
        break;
      case ',':
        wantValue_ = false;
        key_.clear();
        break;
      case '{':
        if (!push(Container::Object)) return false;
        wantValue_ = false;
        key_.clear();
        break;
      case '[':
        if (!push(key_ == "children" ? Container::ChildrenArray : Container::Array)) return false;
        wantValue_ = false;
        key_.clear();
        break;
      case '}':
      case ']':
        pop();
        if (failed_) return false;
        wantValue_ = false;
        key_.clear();
        break;
      default:
        break;
    }
  }
  return true;
}

}  // namespace hn
