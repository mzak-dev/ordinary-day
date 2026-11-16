#include "json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace od {

namespace {
const Json k_null;
const std::string k_empty_string;
const std::vector<Json> k_empty_array;
const std::vector<std::pair<std::string, Json>> k_empty_object;
} // namespace

class JsonParser {
public:
	JsonParser(const std::string &text) : s_(text) {}

	bool parse(Json &out, std::string *error) {
		skip_ws();
		if (!value(out)) {
			if (error) {
				*error = err_ + " (line " + std::to_string(line()) + ")";
			}
			return false;
		}
		skip_ws();
		if (i_ != s_.size()) {
			if (error) {
				*error = "trailing characters (line " + std::to_string(line()) + ")";
			}
			return false;
		}
		return true;
	}

private:
	const std::string &s_;
	size_t i_ = 0;
	std::string err_;

	int line() const {
		int l = 1;
		for (size_t k = 0; k < i_ && k < s_.size(); ++k) {
			if (s_[k] == '\n') {
				++l;
			}
		}
		return l;
	}

	bool fail(const std::string &msg) {
		if (err_.empty()) {
			err_ = msg;
		}
		return false;
	}

	void skip_ws() {
		while (i_ < s_.size()) {
			char c = s_[i_];
			if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
				++i_;
			} else if (c == '/' && i_ + 1 < s_.size() && s_[i_ + 1] == '/') {
				// Line comments are allowed to make content files easier to annotate.
				while (i_ < s_.size() && s_[i_] != '\n') {
					++i_;
				}
			} else {
				break;
			}
		}
	}

	bool value(Json &out) {
		if (i_ >= s_.size()) {
			return fail("unexpected end of input");
		}
		char c = s_[i_];
		if (c == '{') {
			return object(out);
		}
		if (c == '[') {
			return array(out);
		}
		if (c == '"') {
			std::string str;
			if (!string(str)) {
				return false;
			}
			out = Json(std::move(str));
			return true;
		}
		if (c == '-' || (c >= '0' && c <= '9')) {
			return number(out);
		}
		if (s_.compare(i_, 4, "true") == 0) {
			i_ += 4;
			out = Json(true);
			return true;
		}
		if (s_.compare(i_, 5, "false") == 0) {
			i_ += 5;
			out = Json(false);
			return true;
		}
		if (s_.compare(i_, 4, "null") == 0) {
			i_ += 4;
			out = Json();
			return true;
		}
		return fail(std::string("unexpected character '") + c + "'");
	}

	bool object(Json &out) {
		out = Json::object();
		++i_;
		skip_ws();
		if (i_ < s_.size() && s_[i_] == '}') {
			++i_;
			return true;
		}
		while (true) {
			skip_ws();
			if (i_ >= s_.size() || s_[i_] != '"') {
				return fail("expected object key");
			}
			std::string key;
			if (!string(key)) {
				return false;
			}
			skip_ws();
			if (i_ >= s_.size() || s_[i_] != ':') {
				return fail("expected ':'");
			}
			++i_;
			skip_ws();
			Json v;
			if (!value(v)) {
				return false;
			}
			out.set(key, std::move(v));
			skip_ws();
			if (i_ < s_.size() && s_[i_] == ',') {
				++i_;
				skip_ws();
				if (i_ < s_.size() && s_[i_] == '}') {
					++i_;
					return true;
				}
				continue;
			}
			if (i_ < s_.size() && s_[i_] == '}') {
				++i_;
				return true;
			}
			return fail("expected ',' or '}'");
		}
	}

	bool array(Json &out) {
		out = Json::array();
		++i_;
		skip_ws();
		if (i_ < s_.size() && s_[i_] == ']') {
			++i_;
			return true;
		}
		while (true) {
			skip_ws();
			Json v;
			if (!value(v)) {
				return false;
			}
			out.push_back(std::move(v));
			skip_ws();
			if (i_ < s_.size() && s_[i_] == ',') {
				++i_;
				skip_ws();
				if (i_ < s_.size() && s_[i_] == ']') {
					++i_;
					return true;
				}
				continue;
			}
			if (i_ < s_.size() && s_[i_] == ']') {
				++i_;
				return true;
			}
			return fail("expected ',' or ']'");
		}
	}

	static void append_utf8(std::string &out, unsigned cp) {
		if (cp < 0x80) {
			out += char(cp);
		} else if (cp < 0x800) {
			out += char(0xC0 | (cp >> 6));
			out += char(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			out += char(0xE0 | (cp >> 12));
			out += char(0x80 | ((cp >> 6) & 0x3F));
			out += char(0x80 | (cp & 0x3F));
		} else {
			out += char(0xF0 | (cp >> 18));
			out += char(0x80 | ((cp >> 12) & 0x3F));
			out += char(0x80 | ((cp >> 6) & 0x3F));
			out += char(0x80 | (cp & 0x3F));
		}
	}

	bool string(std::string &out) {
		++i_;
		while (i_ < s_.size()) {
			char c = s_[i_++];
			if (c == '"') {
				return true;
			}
			if (c != '\\') {
				out += c;
				continue;
			}
			if (i_ >= s_.size()) {
				break;
			}
			char e = s_[i_++];
			switch (e) {
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case '/': out += '/'; break;
				case 'b': out += '\b'; break;
				case 'f': out += '\f'; break;
				case 'n': out += '\n'; break;
				case 'r': out += '\r'; break;
				case 't': out += '\t'; break;
				case 'u': {
					if (i_ + 4 > s_.size()) {
						return fail("bad unicode escape");
					}
					unsigned cp = unsigned(std::strtoul(s_.substr(i_, 4).c_str(), nullptr, 16));
					i_ += 4;
					if (cp >= 0xD800 && cp <= 0xDBFF && i_ + 6 <= s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
						unsigned lo = unsigned(std::strtoul(s_.substr(i_ + 2, 4).c_str(), nullptr, 16));
						i_ += 6;
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
					}
					append_utf8(out, cp);
					break;
				}
				default:
					return fail("bad escape");
			}
		}
		return fail("unterminated string");
	}

	bool number(Json &out) {
		const char *start = s_.c_str() + i_;
		char *end = nullptr;
		double v = std::strtod(start, &end);
		if (end == start) {
			return fail("bad number");
		}
		i_ += size_t(end - start);
		out = Json(v);
		return true;
	}
};

Json Json::parse(const std::string &text, std::string *error) {
	Json out;
	JsonParser p(text);
	if (!p.parse(out, error)) {
		return Json();
	}
	return out;
}

bool Json::as_bool(bool fallback) const {
	if (is_bool() || is_number()) {
		return num_ != 0.0;
	}
	return fallback;
}

const std::string &Json::as_string() const {
	return is_string() ? str_ : k_empty_string;
}

size_t Json::size() const {
	if (is_array()) {
		return arr_.size();
	}
	if (is_object()) {
		return obj_.size();
	}
	return 0;
}

const Json &Json::operator[](size_t i) const {
	if (is_array() && i < arr_.size()) {
		return arr_[i];
	}
	return k_null;
}

void Json::push_back(Json v) {
	if (type_ != Type::Array) {
		*this = array();
	}
	arr_.push_back(std::move(v));
}

const std::vector<Json> &Json::items() const {
	return is_array() ? arr_ : k_empty_array;
}

bool Json::has(const std::string &key) const {
	if (!is_object()) {
		return false;
	}
	for (const auto &kv : obj_) {
		if (kv.first == key) {
			return true;
		}
	}
	return false;
}

const Json &Json::operator[](const std::string &key) const {
	if (is_object()) {
		for (const auto &kv : obj_) {
			if (kv.first == key) {
				return kv.second;
			}
		}
	}
	return k_null;
}

Json &Json::set(const std::string &key, Json v) {
	if (type_ != Type::Object) {
		*this = object();
	}
	for (auto &kv : obj_) {
		if (kv.first == key) {
			kv.second = std::move(v);
			return kv.second;
		}
	}
	obj_.emplace_back(key, std::move(v));
	return obj_.back().second;
}

const std::vector<std::pair<std::string, Json>> &Json::members() const {
	return is_object() ? obj_ : k_empty_object;
}

std::vector<std::string> Json::get_strings(const std::string &key) const {
	std::vector<std::string> out;
	const Json &v = (*this)[key];
	if (v.is_string()) {
		out.push_back(v.as_string());
	} else {
		for (const Json &e : v.items()) {
			if (e.is_string()) {
				out.push_back(e.as_string());
			}
		}
	}
	return out;
}

static void escape_to(std::string &out, const std::string &s) {
	out += '"';
	for (char c : s) {
		switch (c) {
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20) {
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04x", c);
					out += buf;
				} else {
					out += c;
				}
		}
	}
	out += '"';
}

void Json::dump_to(std::string &out, int indent, int depth) const {
	auto newline = [&](int d) {
		if (indent >= 0) {
			out += '\n';
			out.append(size_t(indent * d), ' ');
		}
	};
	switch (type_) {
		case Type::Null: out += "null"; break;
		case Type::Bool: out += num_ != 0.0 ? "true" : "false"; break;
		case Type::Number: {
			if (std::floor(num_) == num_ && std::fabs(num_) < 1e15) {
				out += std::to_string((long long)num_);
			} else {
				char buf[32];
				std::snprintf(buf, sizeof(buf), "%.17g", num_);
				out += buf;
			}
			break;
		}
		case Type::String: escape_to(out, str_); break;
		case Type::Array: {
			out += '[';
			for (size_t i = 0; i < arr_.size(); ++i) {
				if (i) {
					out += ',';
				}
				newline(depth + 1);
				arr_[i].dump_to(out, indent, depth + 1);
			}
			if (!arr_.empty()) {
				newline(depth);
			}
			out += ']';
			break;
		}
		case Type::Object: {
			out += '{';
			for (size_t i = 0; i < obj_.size(); ++i) {
				if (i) {
					out += ',';
				}
				newline(depth + 1);
				escape_to(out, obj_[i].first);
				out += indent >= 0 ? ": " : ":";
				obj_[i].second.dump_to(out, indent, depth + 1);
			}
			if (!obj_.empty()) {
				newline(depth);
			}
			out += '}';
			break;
		}
	}
}

std::string Json::dump(int indent) const {
	std::string out;
	dump_to(out, indent, 0);
	return out;
}

} // namespace od
