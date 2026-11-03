#include "RssFeed.h"

#include <XmlParserUtils.h>
#include <expat.h>

#include <cctype>
#include <cstring>

namespace rss {
namespace {

XML_Parser asParser(void* p) { return static_cast<XML_Parser>(p); }

bool streq(const char* a, const char* b) { return std::strcmp(a, b) == 0; }

char lower(const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool equalsFold(const std::string_view a, const std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (lower(a[i]) != lower(b[i])) return false;
  }
  return true;
}

bool containsFold(const std::string_view haystack, const std::string_view needle) {
  if (needle.empty() || haystack.size() < needle.size()) return false;
  for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
    if (equalsFold(haystack.substr(i, needle.size()), needle)) return true;
  }
  return false;
}

std::string trimmed(const std::string& text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) --end;
  return text.substr(begin, end - begin);
}

const char* attr(const char** attrs, const char* name) {
  if (attrs == nullptr) return nullptr;
  for (int i = 0; attrs[i] != nullptr; i += 2) {
    if (streq(xmlLocalName(attrs[i]), name)) return attrs[i + 1];
  }
  return nullptr;
}

void appendEscaped(std::string& out, const char* data, const size_t length) {
  for (size_t i = 0; i < length; ++i) {
    switch (data[i]) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      default:
        out.push_back(data[i]);
    }
  }
}

// Windows-1252, which expat does not know and feeds still declare. Bytes
// 0x80..0x9F are its punctuation; everything else is Latin-1. ISO-8859-15 is
// read the same way, which is right for every byte but eight rare symbols.
// The five bytes Windows-1252 leaves undefined stay their own C1 codepoint:
// expat refuses a map that sends a high byte to ASCII punctuation.
constexpr uint16_t kCp1252[32] = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                  0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
                                  0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                  0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

int XMLCALL unknownEncoding(void*, const XML_Char* name, XML_Encoding* info) {
  if (name == nullptr || info == nullptr) return XML_STATUS_ERROR;
  const std::string_view n(name);
  if (!equalsFold(n, "windows-1252") && !equalsFold(n, "cp1252") && !equalsFold(n, "x-cp1252") &&
      !equalsFold(n, "iso-8859-15") && !equalsFold(n, "latin9")) {
    return XML_STATUS_ERROR;
  }
  for (int i = 0; i < 256; ++i) info->map[i] = i;
  for (int i = 0x80; i < 0xA0; ++i) info->map[i] = kCp1252[i - 0x80];
  info->data = nullptr;
  info->convert = nullptr;
  info->release = nullptr;
  return XML_STATUS_OK;
}

// Atom's type="" decides how the words inside an element are written.
bool atomMode(const char** attrs, bool& html, bool& xhtml) {
  const char* type = attr(attrs, "type");
  html = false;
  xhtml = false;
  if (type == nullptr || equalsFold(type, "text") || equalsFold(type, "text/plain")) return true;
  if (equalsFold(type, "html") || equalsFold(type, "text/html")) {
    html = true;
    return true;
  }
  if (equalsFold(type, "xhtml") || equalsFold(type, "application/xhtml+xml")) {
    xhtml = true;
    return true;
  }
  return false;  // an image, a PDF: not words
}

bool looksLikeWebUrl(const std::string& text) {
  return text.rfind("http://", 0) == 0 || text.rfind("https://", 0) == 0;
}

// Plain text (Atom type="text") that has been escaped into markup: a blank
// line between paragraphs becomes a paragraph break, so the words keep the
// shape the author gave them.
std::string paragraphsMarked(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  size_t newlines = 0;
  for (const char c : text) {
    if (c == '\n') {
      ++newlines;
      continue;
    }
    if (c == '\r') continue;
    if (newlines >= 2) {
      out += "<p>";
    } else if (newlines == 1) {
      out.push_back(' ');
    }
    newlines = 0;
    out.push_back(c);
  }
  return out;
}

}  // namespace

void Item::clear() {
  title.clear();
  link.clear();
  guid.clear();
  dateText.clear();
  content.clear();
  summary.clear();
  clipped = false;
}

// --- EntityFixer ---------------------------------------------------------

namespace {
constexpr std::string_view kCdataOpen = "<![CDATA[";
constexpr std::string_view kCommentOpen = "<!--";

// A character XML allows at all. A reference to any other is a fatal error to
// expat, however well-formed the reference itself.
bool xmlChar(const unsigned long cp) {
  return cp == 0x9 || cp == 0xA || cp == 0xD || (cp >= 0x20 && cp <= 0xD7FF) || (cp >= 0xE000 && cp <= 0xFFFD) ||
         (cp >= 0x10000 && cp <= 0x10FFFF);
}

bool validReference(const std::string_view name) {
  if (name == "amp" || name == "lt" || name == "gt" || name == "quot" || name == "apos") return true;
  if (name.size() < 2 || name[0] != '#') return false;
  const bool hex = name[1] == 'x' || name[1] == 'X';
  const std::string_view digits = name.substr(hex ? 2 : 1);
  if (digits.empty() || digits.size() > 8) return false;
  unsigned long value = 0;
  for (const char c : digits) {
    if (hex ? std::isxdigit(static_cast<unsigned char>(c)) == 0 : std::isdigit(static_cast<unsigned char>(c)) == 0) {
      return false;
    }
    value =
        value * (hex ? 16 : 10) +
        static_cast<unsigned long>(std::isdigit(static_cast<unsigned char>(c)) != 0 ? c - '0' : (lower(c) - 'a' + 10));
  }
  return xmlChar(value);
}

// A raw byte XML forbids anywhere: every C0 control but tab and the line ends.
bool forbiddenByte(const char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  return u < 0x20 && u != 0x9 && u != 0xA && u != 0xD;
}
}  // namespace

void EntityFixer::feed(const char* data, const size_t length, std::string& out) {
  static constexpr unsigned char kBom[] = {0xEF, 0xBB, 0xBF};
  for (size_t i = 0; i < length; ++i) {
    const char c = data[i];
    const unsigned char u = static_cast<unsigned char>(c);
    if (!started_) {
      started_ = true;
      passthrough_ = u == 0x00 || u == 0xFE || u == 0xFF;
    }
    if (passthrough_) {
      out.push_back(c);
      continue;
    }
    if (leading_) {
      if (bomMatched_ < 3 && u == kBom[bomMatched_]) {
        out.push_back(c);
        ++bomMatched_;
        continue;
      }
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
      leading_ = false;
    }
    if (forbiddenByte(c)) continue;
    text(c, out);
  }
}

void EntityFixer::endEntity(const char next, const bool terminated, std::string& out) {
  (void)next;
  const std::string_view name = std::string_view(held_).substr(1);
  if (terminated && validReference(name)) {
    out += held_;
    out.push_back(';');
  } else {
    out += "&amp;";
    out.append(name);
    if (terminated) out.push_back(';');
  }
  held_.clear();
  state_ = State::Text;
}

void EntityFixer::text(const char c, std::string& out) {
  switch (state_) {
    case State::Text:
      if (c == '&') {
        state_ = State::Entity;
        held_ = "&";
      } else if (c == '<') {
        state_ = State::Markup;
        held_ = "<";
      } else {
        out.push_back(c);
      }
      return;

    case State::Markup: {
      held_.push_back(c);
      if (held_ == kCdataOpen) {
        out += held_;
        held_.clear();
        state_ = State::Cdata;
        closeMatch_ = 0;
        return;
      }
      if (held_ == kCommentOpen) {
        out += held_;
        held_.clear();
        state_ = State::Comment;
        closeMatch_ = 0;
        return;
      }
      if (kCdataOpen.substr(0, held_.size()) == held_ || kCommentOpen.substr(0, held_.size()) == held_) return;
      // An ordinary tag. Everything but the last byte goes out as it came, and
      // the last byte is read again as text, because it may be an '&' inside an
      // attribute value or another '<'.
      held_.pop_back();
      out += held_;
      held_.clear();
      state_ = State::Text;
      text(c, out);
      return;
    }

    case State::Cdata:
    case State::Comment: {
      out.push_back(c);
      const char closer = state_ == State::Cdata ? ']' : '-';
      if (c == closer) {
        if (closeMatch_ < 2) ++closeMatch_;
      } else if (c == '>' && closeMatch_ == 2) {
        state_ = State::Text;
        closeMatch_ = 0;
      } else {
        closeMatch_ = 0;
      }
      return;
    }

    case State::Entity:
      if (c == ';') {
        endEntity(c, true, out);
      } else if ((std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '#') && held_.size() < 33) {
        held_.push_back(c);
      } else {
        endEntity(c, false, out);
        text(c, out);
      }
      return;
  }
}

void EntityFixer::finish(std::string& out) {
  if (state_ == State::Entity) {
    endEntity('\0', false, out);
  } else if (state_ == State::Markup) {
    out += held_;
    held_.clear();
  }
  state_ = State::Text;
}

// --- FeedParser ----------------------------------------------------------

FeedParser::FeedParser(ItemSink* sink, const ParserLimits& limits) : sink_(sink), limits_(limits) {
  XML_Parser parser = XML_ParserCreate(nullptr);
  if (parser == nullptr) {
    done_ = true;
    malformed_ = true;
    return;
  }
  XML_SetUserData(parser, this);
  XML_SetElementHandler(parser, &FeedParser::onStart, &FeedParser::onEnd);
  XML_SetCharacterDataHandler(parser, &FeedParser::onText);
  XML_SetUnknownEncodingHandler(parser, &unknownEncoding, nullptr);
  parser_ = parser;
}

FeedParser::~FeedParser() {
  XML_Parser parser = asParser(parser_);
  destroyXmlParser(parser);
  parser_ = nullptr;
}

void FeedParser::onStart(void* self, const char* name, const char** attrs) {
  static_cast<FeedParser*>(self)->start(name, attrs);
}
void FeedParser::onEnd(void* self, const char* name) { static_cast<FeedParser*>(self)->end(name); }
void FeedParser::onText(void* self, const char* text, const int length) {
  static_cast<FeedParser*>(self)->text(text, length);
}

void FeedParser::stop(const bool bySink) {
  done_ = true;
  stoppedBySink_ = bySink;
  if (parser_ != nullptr) XML_StopParser(asParser(parser_), XML_FALSE);
}

void FeedParser::begin(const Field field, const Mode mode, const size_t limit) {
  field_ = field;
  mode_ = mode;
  fieldDepth_ = depth_;
  fieldLimit_ = limit;
  fieldClipped_ = false;
  buffer_.clear();
}

void FeedParser::start(const char* name, const char** attrs) {
  if (done_) return;
  ++depth_;
  const char* local = xmlLocalName(name);
  const bool prefixed = local != name;

  if (depth_ == 1) {
    if (!prefixed && streq(local, "rss")) {
      kind_ = Kind::Rss;
    } else if (streq(local, "feed")) {
      kind_ = Kind::Atom;
    } else if (streq(local, "RDF")) {
      kind_ = Kind::Rdf;
    } else {
      kind_ = Kind::NotFeed;
      stop(false);
    }
    return;
  }

  // Inside a field: its children are part of its words. Markup fields keep
  // them as tags so the paragraph breaks survive; attributes are not words.
  if (field_ != Field::None) {
    if (mode_ == Mode::Html || mode_ == Mode::Xhtml) {
      if (buffer_.size() + std::strlen(local) + 2 <= fieldLimit_) {
        buffer_.push_back('<');
        buffer_ += local;
        buffer_.push_back('>');
      }
    }
    return;
  }

  // Inside Atom every element is the format's own; inside RSS a prefixed one
  // is an extension, and only the few below mean anything.
  const bool own = kind_ == Kind::Atom || !prefixed;

  if (!inItem_) {
    const bool opensItem = own && (kind_ == Kind::Atom ? streq(local, "entry") : streq(local, "item"));
    if (opensItem) {
      inItem_ = true;
      itemDepth_ = depth_;
      item_.clear();
      datePriority_ = 0;
      haveAltLink_ = false;
      if (kind_ == Kind::Rdf) {
        const char* about = attr(attrs, "about");
        if (about != nullptr) item_.guid.assign(about, strnlen(about, limits_.linkBytes));
      }
      return;
    }
    if (own && kind_ != Kind::Atom && streq(local, "channel")) {
      inChannel_ = true;
      return;
    }
    // The feed's own name and site: directly under <feed>, or directly under
    // <channel>. Not anywhere deeper: an <image> has a <title> and a <link> too.
    const bool feedLevel = kind_ == Kind::Atom ? depth_ == 2 : (inChannel_ && depth_ == 3);
    if (!own || !feedLevel) return;
    if (streq(local, "title") && title_.empty()) {
      begin(Field::FeedTitle, Mode::Plain, limits_.titleBytes);
    } else if (streq(local, "link")) {
      if (kind_ == Kind::Atom) {
        const char* rel = attr(attrs, "rel");
        const char* type = attr(attrs, "type");
        const char* href = attr(attrs, "href");
        if (href != nullptr && siteLink_.empty() && (rel == nullptr || streq(rel, "alternate")) &&
            (type == nullptr || containsFold(type, "html"))) {
          siteLink_.assign(href, strnlen(href, limits_.linkBytes));
        }
      } else if (siteLink_.empty()) {
        begin(Field::FeedLink, Mode::Plain, limits_.linkBytes);
      }
    }
    return;
  }

  // In a post. Only its direct children are fields.
  if (depth_ != itemDepth_ + 1) return;

  if (kind_ == Kind::Atom) {
    bool html = false;
    bool xhtml = false;
    if (streq(local, "title")) {
      atomMode(attrs, html, xhtml);
      begin(Field::Title, xhtml ? Mode::Xhtml : Mode::Plain, limits_.titleBytes);
    } else if (streq(local, "link")) {
      const char* rel = attr(attrs, "rel");
      const char* href = attr(attrs, "href");
      if (href != nullptr && !haveAltLink_ && (rel == nullptr || streq(rel, "alternate"))) {
        item_.link.assign(href, strnlen(href, limits_.linkBytes));
        haveAltLink_ = true;
      }
    } else if (streq(local, "id")) {
      begin(Field::Guid, Mode::Plain, limits_.linkBytes);
    } else if (streq(local, "published") || streq(local, "issued") || streq(local, "updated") ||
               streq(local, "modified")) {
      pendingDatePriority_ = (streq(local, "published") || streq(local, "issued")) ? 4 : 1;
      begin(Field::Date, Mode::Plain, 64);
    } else if (streq(local, "content") || streq(local, "summary")) {
      const bool content = streq(local, "content");
      // Content by reference (src=) is a pointer, not words.
      if (content && attr(attrs, "src") != nullptr) return;
      if (!atomMode(attrs, html, xhtml)) return;
      const Mode mode = html ? Mode::Html : (xhtml ? Mode::Xhtml : Mode::Text);
      begin(content ? Field::Content : Field::Summary, mode, content ? limits_.contentBytes : limits_.summaryBytes);
    }
    return;
  }

  if (!prefixed) {
    if (streq(local, "title")) {
      begin(Field::Title, Mode::Plain, limits_.titleBytes);
    } else if (streq(local, "link")) {
      begin(Field::Link, Mode::Plain, limits_.linkBytes);
    } else if (streq(local, "guid")) {
      begin(Field::Guid, Mode::Plain, limits_.linkBytes);
    } else if (streq(local, "pubDate")) {
      pendingDatePriority_ = 3;
      begin(Field::Date, Mode::Plain, 64);
    } else if (streq(local, "description")) {
      begin(Field::Summary, Mode::Html, limits_.summaryBytes);
    }
    return;
  }
  if (streq(local, "encoded")) {
    begin(Field::Content, Mode::Html, limits_.contentBytes);
  } else if (streq(local, "date")) {
    pendingDatePriority_ = 2;
    begin(Field::Date, Mode::Plain, 64);
  }
}

void FeedParser::text(const char* data, const int length) {
  if (done_ || field_ == Field::None || length <= 0) return;
  size_t take = static_cast<size_t>(length);
  if (buffer_.size() >= fieldLimit_) {
    fieldClipped_ = true;
    return;
  }
  if (buffer_.size() + take > fieldLimit_) {
    take = fieldLimit_ - buffer_.size();
    fieldClipped_ = true;
  }
  if (mode_ == Mode::Xhtml || mode_ == Mode::Text) {
    appendEscaped(buffer_, data, take);
  } else {
    buffer_.append(data, take);
  }
}

void FeedParser::commit() {
  switch (field_) {
    case Field::Title:
      if (item_.title.empty()) item_.title = buffer_;
      break;
    case Field::Link:
      if (item_.link.empty()) item_.link = trimmed(buffer_);
      break;
    case Field::Guid:
      if (item_.guid.empty()) item_.guid = trimmed(buffer_);
      break;
    case Field::Date:
      if (pendingDatePriority_ > datePriority_) {
        item_.dateText = trimmed(buffer_);
        datePriority_ = pendingDatePriority_;
      }
      break;
    case Field::Content:
    case Field::Summary: {
      std::string& target = field_ == Field::Content ? item_.content : item_.summary;
      if (target.empty()) {
        // Swapped rather than copied: a post's markup can be 96KB, and the
        // buffer it leaves behind is reused by the next field.
        if (mode_ == Mode::Text) {
          target = paragraphsMarked(buffer_);
        } else {
          target.swap(buffer_);
        }
        item_.clipped = item_.clipped || fieldClipped_;
      }
      break;
    }
    case Field::FeedTitle:
      title_ = buffer_;
      break;
    case Field::FeedLink:
      siteLink_ = trimmed(buffer_);
      break;
    case Field::None:
      break;
  }
  field_ = Field::None;
  // Not shrunk: the next post's content reuses the allocation.
  buffer_.clear();
}

void FeedParser::end(const char* name) {
  if (done_) return;
  const char* local = xmlLocalName(name);
  if (field_ != Field::None) {
    if (depth_ > fieldDepth_) {
      if ((mode_ == Mode::Html || mode_ == Mode::Xhtml) && buffer_.size() + std::strlen(local) + 3 <= fieldLimit_) {
        buffer_ += "</";
        buffer_ += local;
        buffer_.push_back('>');
      }
      --depth_;
      return;
    }
    commit();
    --depth_;
    return;
  }
  if (inItem_ && depth_ == itemDepth_) {
    inItem_ = false;
    ++items_;
    if (sink_ != nullptr && !sink_->onItem(item_)) stop(true);
    --depth_;
    return;
  }
  if (inChannel_ && depth_ == 2 && streq(local, "channel")) inChannel_ = false;
  --depth_;
}

bool FeedParser::feed(const char* data, const size_t length) {
  if (done_ || parser_ == nullptr) return false;
  scratch_.clear();
  fixer_.feed(data, length, scratch_);
  if (scratch_.empty()) return true;
  const XML_Status status = XML_Parse(asParser(parser_), scratch_.data(), static_cast<int>(scratch_.size()), XML_FALSE);
  if (status == XML_STATUS_ERROR && !done_) {
    malformed_ = true;
    done_ = true;
  }
  return !done_;
}

void FeedParser::finish() {
  if (done_ || parser_ == nullptr) return;
  scratch_.clear();
  fixer_.finish(scratch_);
  const XML_Status status = XML_Parse(asParser(parser_), scratch_.data(), static_cast<int>(scratch_.size()), XML_TRUE);
  // A feed cut off at the byte limit ends mid-element, which expat calls an
  // error. The posts before the cut already reached the sink.
  if (status == XML_STATUS_ERROR && !done_) malformed_ = true;
  done_ = true;
}

std::string FeedParser::title() const { return plainLine(title_); }

// --- LinkScanner ---------------------------------------------------------

void LinkScanner::feed(const char* data, const size_t length) {
  for (size_t i = 0; i < length && !done_; ++i) {
    const char c = data[i];
    switch (state_) {
      case State::Text:
        if (c == '<') {
          state_ = State::Tag;
          tag_.clear();
          tagTooLong_ = false;
          quote_ = 0;
        }
        break;

      case State::Tag:
        if (quote_ != 0) {
          if (c == quote_) quote_ = 0;
        } else if (c == '"' || c == '\'') {
          quote_ = c;
        } else if (c == '>') {
          endTag();
          break;
        }
        if (tag_.size() < 2048) {
          tag_.push_back(c);
        } else {
          tagTooLong_ = true;
        }
        if (tag_ == "!--") {
          state_ = State::Comment;
          commentMatch_ = 0;
        }
        break;

      case State::Comment:
        if (c == '-') {
          if (commentMatch_ < 2) ++commentMatch_;
        } else if (c == '>' && commentMatch_ == 2) {
          state_ = State::Text;
        } else {
          commentMatch_ = 0;
        }
        break;

      case State::Skip:
        if (lower(c) == skipName_[skipMatch_]) {
          if (++skipMatch_ == skipName_.size()) {
            // The closing tag's name has matched; its '>' ends it like any tag.
            state_ = State::Tag;
            tag_ = "/";
            tagTooLong_ = false;
            quote_ = 0;
          }
        } else {
          skipMatch_ = lower(c) == skipName_[0] ? 1 : 0;
        }
        break;
    }
  }
}

void LinkScanner::endTag() {
  state_ = State::Text;
  if (tagTooLong_ || tag_.empty()) return;

  size_t i = 0;
  std::string name;
  while (i < tag_.size() && std::isspace(static_cast<unsigned char>(tag_[i])) == 0 && tag_[i] != '>' &&
         !(tag_[i] == '/' && i > 0)) {
    name.push_back(lower(tag_[i]));
    ++i;
  }
  if (name == "body" || name == "/head") {
    done_ = true;
    return;
  }
  const bool selfClosing = !tag_.empty() && tag_.back() == '/';
  if ((name == "script" || name == "style") && !selfClosing) {
    state_ = State::Skip;
    skipName_ = "</" + name;
    skipMatch_ = 0;
    return;
  }
  if (name != "link") return;

  std::string rel;
  std::string type;
  std::string href;
  while (i < tag_.size()) {
    while (i < tag_.size() && (std::isspace(static_cast<unsigned char>(tag_[i])) != 0 || tag_[i] == '/')) ++i;
    std::string attrName;
    while (i < tag_.size() && tag_[i] != '=' && std::isspace(static_cast<unsigned char>(tag_[i])) == 0 &&
           tag_[i] != '/') {
      attrName.push_back(lower(tag_[i]));
      ++i;
    }
    while (i < tag_.size() && std::isspace(static_cast<unsigned char>(tag_[i])) != 0) ++i;
    std::string value;
    if (i < tag_.size() && tag_[i] == '=') {
      ++i;
      while (i < tag_.size() && std::isspace(static_cast<unsigned char>(tag_[i])) != 0) ++i;
      if (i < tag_.size() && (tag_[i] == '"' || tag_[i] == '\'')) {
        const char q = tag_[i++];
        const size_t close = tag_.find(q, i);
        const size_t stop = close == std::string::npos ? tag_.size() : close;
        value = tag_.substr(i, stop - i);
        i = close == std::string::npos ? tag_.size() : close + 1;
      } else {
        while (i < tag_.size() && std::isspace(static_cast<unsigned char>(tag_[i])) == 0 && tag_[i] != '>') {
          value.push_back(tag_[i]);
          ++i;
        }
      }
    }
    if (attrName.empty()) {
      if (i < tag_.size()) ++i;
      continue;
    }
    if (attrName == "rel") rel = value;
    if (attrName == "type") type = value;
    if (attrName == "href") href = value;
  }

  // rel is a token list ("alternate", "Alternate", sometimes "alternate feed").
  const bool alternate = containsFold(rel, "alternate");
  const bool feedType = equalsFold(trimmed(type), "application/rss+xml") ||
                        equalsFold(trimmed(type), "application/atom+xml") ||
                        equalsFold(trimmed(type), "application/rdf+xml");
  if (!alternate || !feedType) return;
  const std::string decoded = trimmed(decodeEntities(href));
  if (decoded.empty() || feeds_.size() >= 8) return;
  for (const std::string& known : feeds_) {
    if (known == decoded) return;
  }
  feeds_.push_back(decoded);
}

// --- Probe ---------------------------------------------------------------

bool Probe::feed(const char* data, const size_t length) {
  bool more = true;
  if (parser_.kind() == Kind::Unknown || parser_.isFeed()) more = parser_.feed(data, length);
  if (parser_.isFeed()) return more;
  // Not a feed, or not yet known to be one: the same bytes, read as a page.
  links_.feed(data, length);
  return !links_.done();
}

void Probe::finish() {
  if (parser_.isFeed()) parser_.finish();
}

// --- Ingest --------------------------------------------------------------

bool ingest(const Item& item, const std::string& feedUrl, const std::string& feedKey, Post& post, std::string& text) {
  post = Post{};
  text.clear();
  post.feed = feedKey;

  std::string link = trimmed(item.link);
  // An RSS guid is the link unless it says otherwise, and some feeds give only
  // the guid.
  if (link.empty() && looksLikeWebUrl(item.guid)) link = item.guid;
  if (!link.empty()) post.link = resolveUrl(feedUrl, link).substr(0, kMaxLinkBytes);

  // The feed's own id for the post, which survives a changed headline or link.
  // Scoped to the feed: plenty of feeds number their posts from 1, and two
  // feeds' post 7 must not share a file on the card.
  std::string identity = !item.guid.empty() ? item.guid : !post.link.empty() ? post.link : item.title + item.dateText;
  if (identity.empty()) return false;
  post.id = keyFor(feedKey + "\n" + identity);

  post.title = plainLine(item.title);
  post.date = parseDate(item.dateText);

  std::vector<std::string> paragraphs;
  if (!item.content.empty()) {
    paragraphs = paragraphsFromHtml(item.content, kMaxContentBytes);
    if (!paragraphs.empty()) post.body = Body::Full;
  }
  if (paragraphs.empty() && !item.summary.empty()) {
    paragraphs = paragraphsFromHtml(item.summary, kMaxSummaryBytes);
    if (!paragraphs.empty()) post.body = Body::Summary;
  }
  if (!paragraphs.empty()) text = joinParagraphs(paragraphs);

  // A post with no headline (a microblog's, say) is named by its first words.
  if (post.title.empty() && !paragraphs.empty()) {
    post.title = plainLine(paragraphs.front().substr(0, 120));
    if (paragraphs.front().size() > 120) post.title += "...";
  }
  if (post.title.empty()) post.title = post.link.empty() ? std::string("Untitled") : hostOf(post.link);
  return true;
}

}  // namespace rss
