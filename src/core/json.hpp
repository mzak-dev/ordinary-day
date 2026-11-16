#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace od {

// Minimal, exception-free JSON value. Getters are lenient: a missing key or a
// type mismatch yields the provided fallback, so broken content never aborts.
class Json {
public:
	enum class Type { Null, Bool, Number, String, Array, Object };

	Json() = default;
	Json(bool v) : type_(Type::Bool), num_(v ? 1.0 : 0.0) {}
	Json(int v) : type_(Type::Number), num_(v) {}
	Json(long long v) : type_(Type::Number), num_(double(v)) {}
	Json(double v) : type_(Type::Number), num_(v) {}
	Json(const char *v) : type_(Type::String), str_(v) {}
	Json(std::string v) : type_(Type::String), str_(std::move(v)) {}

	static Json array() { Json j; j.type_ = Type::Array; return j; }
	static Json object() { Json j; j.type_ = Type::Object; return j; }

	static Json parse(const std::string &text, std::string *error = nullptr);
	std::string dump(int indent = -1) const;

	Type type() const { return type_; }
	bool is_null() const { return type_ == Type::Null; }
	bool is_bool() const { return type_ == Type::Bool; }
	bool is_number() const { return type_ == Type::Number; }
	bool is_string() const { return type_ == Type::String; }
	bool is_array() const { return type_ == Type::Array; }
	bool is_object() const { return type_ == Type::Object; }

	bool as_bool(bool fallback = false) const;
	double as_number(double fallback = 0.0) const { return is_number() || is_bool() ? num_ : fallback; }
	int as_int(int fallback = 0) const { return is_number() || is_bool() ? int(num_) : fallback; }
	const std::string &as_string() const;
	std::string as_string(const std::string &fallback) const { return is_string() ? str_ : fallback; }

	// Arrays
	size_t size() const;
	const Json &operator[](size_t i) const;
	void push_back(Json v);
	const std::vector<Json> &items() const;

	// Objects
	bool has(const std::string &key) const;
	const Json &operator[](const std::string &key) const;
	Json &set(const std::string &key, Json v);
	const std::vector<std::pair<std::string, Json>> &members() const;

	std::string get_string(const std::string &key, const std::string &fallback = "") const { return (*this)[key].as_string(fallback); }
	int get_int(const std::string &key, int fallback = 0) const { return (*this)[key].as_int(fallback); }
	double get_number(const std::string &key, double fallback = 0.0) const { return (*this)[key].as_number(fallback); }
	bool get_bool(const std::string &key, bool fallback = false) const { return (*this)[key].as_bool(fallback); }

	// Accepts a single string or an array of strings.
	std::vector<std::string> get_strings(const std::string &key) const;

private:
	Type type_ = Type::Null;
	double num_ = 0.0;
	std::string str_;
	std::vector<Json> arr_;
	std::vector<std::pair<std::string, Json>> obj_;

	void dump_to(std::string &out, int indent, int depth) const;
	friend class JsonParser;
};

} // namespace od
