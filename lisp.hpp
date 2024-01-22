#pragma once
#include <iostream>
#include <sstream>
#include <string>
#include <memory>
#include <vector>
#include <map>
#include <stdint.h>
#include <fstream>
#include <ctime>
#include <functional>
#include <type_traits>

constexpr auto TailCallOptimisation = true;

class Env;

class Base_Object {
public:
	virtual ~Base_Object() = default;

	template<typename T>
		requires std::is_base_of_v<Base_Object, T>
	bool typep() const {
		return typeid(*this) == typeid(T);
	}

	template<typename T>
		requires std::is_base_of_v<Base_Object, T>
	T& getAs() {
		return dynamic_cast<T&>(*this);
	}

	template<typename T>
		requires std::is_base_of_v<Base_Object, T>
	const T& getAs() const {
		return dynamic_cast<const T&>(*this);
	}

	virtual std::ostream& operator<<(std::ostream& os) const = 0;

	friend bool operator==(const Base_Object& l, const Base_Object& r)
	{
		return &l == &r;
	}

	bool isnull() const;
};

using BObjSharedPtr = std::shared_ptr<Base_Object>;
using EnvSPtr = std::shared_ptr<Env>;
using EnvWPtr = std::weak_ptr<Env>;

extern int totalSym;
extern std::map<std::string, BObjSharedPtr> sMap;
extern EnvSPtr Environment;

class Cons : public Base_Object {
public:
	BObjSharedPtr car;
	BObjSharedPtr cdr;

	Cons(BObjSharedPtr a, BObjSharedPtr d)
		: car(std::move(a)), cdr(std::move(d)) {}

	std::ostream& operator<<(std::ostream& os) const override {
		os << "(";
		car->operator<<(os);

		Base_Object* o = cdr.get();
		while (true) {
			if (o->typep<Cons>()) {
				os << " ";
				Cons* nestedCons = &o->getAs<Cons>();
				nestedCons->car->operator<<(os);
				o = nestedCons->cdr.get();
			}
			else if (o->isnull()) {
				break;
			}
			else {
				os << " . ";
				o->operator<<(os);
				break;
			}
		}
		os << ")";
		return os;
	}
};

class Symbol : public Base_Object {
public:
	const std::string name;

	Symbol(const std::string n)
		: name(n) {}

	std::ostream& operator<<(std::ostream& os) const override {
		os << name;
		return os;
	}
};

class Integer : public Base_Object {
public:
	int value;

	Integer(int v)
		: value(v) {}

	std::ostream& operator<<(std::ostream& os) const override {
		os << value;
		return os;
	}
	friend bool operator==(const Integer& l, const Integer& r)
	{
		return r.typep<Integer>() && l.value == r.getAs<Integer>().value;
	}
};

class String : public Base_Object {
public:
	std::string value;

	String(const std::string& v)
		: value(v) {}

	std::ostream& operator<<(std::ostream& os) const override {
		os << value;
		return os;
	}
	friend bool operator==(const String& l, const String& r)
	{
		return r.typep<String>() && l.value == r.getAs<String>().value;
	}
};


class Proc : public Base_Object {
public:
	BObjSharedPtr parameterList;
	BObjSharedPtr body;
	EnvSPtr env;
	Proc(BObjSharedPtr pl, BObjSharedPtr b, EnvSPtr e)
		: parameterList(pl), body(b), env(e) {}
	std::ostream& operator<<(std::ostream& os) const override {
		os << "<Proc>";
		return os;
	}
};

class PredefinedProc : public Base_Object {
public:
	std::function<BObjSharedPtr(Env& env, std::vector<BObjSharedPtr>&)> function;

	PredefinedProc(std::function<BObjSharedPtr(Env& env, std::vector<BObjSharedPtr>&)> f)
		: function(f) {}

	std::ostream& operator<<(std::ostream& os) const override {
		os << "<PredefinedProc>";
		return os;
	}
};

class Macro : public Base_Object {
public:
	BObjSharedPtr parameterList;
	BObjSharedPtr body;
	EnvSPtr env;

	Macro(BObjSharedPtr pl, BObjSharedPtr b, EnvSPtr e)
		: parameterList(pl), body(b), env(e) {}

	std::ostream& operator<<(std::ostream& os) const override {
		os << "<Macro>";
		return os;
	}
};


BObjSharedPtr readParse(Env& env, std::istream& is);

extern BObjSharedPtr registerSymbol(std::string name);

class Env {
private:
	EnvWPtr envobj;
	EnvSPtr outEnvironment;
	EnvSPtr environmentLex;
	std::map<Symbol*, BObjSharedPtr> symbolValueMap;
	bool closed = true;

public:
	Env();
	Env(EnvSPtr e, EnvSPtr l)
		: outEnvironment(e), environmentLex(l), closed(false) {}

	static EnvSPtr createEnvironment() {
		EnvSPtr env = std::make_shared<Env>();
		env->envobj = env;
		return env;
	}

	EnvSPtr createSubEnvironment(EnvSPtr l = nullptr) const {
		EnvSPtr env = std::make_shared<Env>(EnvSPtr(envobj), l);
		env->envobj = env;
		return env;
	}

	EnvSPtr findEnvironment(Symbol* symbol) const {
		if (isSpecialVariable(symbol))
			return resolveEnvDyn(symbol);
		else
			return resolveEnvLex(symbol);
	}

	EnvSPtr resolveEnvDyn(Symbol* symbol) const {
		if (symbolValueMap.count(symbol))
			return EnvSPtr(envobj);
		if (outEnvironment != nullptr)
			return outEnvironment->resolveEnvDyn(symbol);
		return EnvSPtr(nullptr);
	}

	EnvSPtr resolveEnvLex(Symbol* symbol) const {
		if (symbolValueMap.count(symbol))
			return EnvSPtr(envobj);
		if (environmentLex != nullptr)
			return environmentLex->resolveEnvLex(symbol);
		if (outEnvironment != nullptr)
			return outEnvironment->resolveEnvLex(symbol);
		return EnvSPtr(nullptr);
	}

	BObjSharedPtr findSymbolInMap(Symbol* symbol) {
		EnvSPtr env = findEnvironment(symbol);
		if (env == nullptr) return BObjSharedPtr(nullptr);
		return env->symbolValueMap[symbol];
	}

	void bind(BObjSharedPtr objPtr, Symbol* symbol) {
		symbolValueMap[symbol] = objPtr;
	}

	bool isSpecialVariable(Symbol* symbol) const {
		return Environment->symbolValueMap.count(symbol);
	}

	void merge(EnvSPtr env) {
		for (auto& kv : env->symbolValueMap) {
			symbolValueMap[kv.first] = kv.second;
		}
		if (env->environmentLex != nullptr)
			environmentLex = env->environmentLex;
	}

	void setLexEnv(EnvSPtr l) {
		if (l != nullptr)
			environmentLex = l;
	}

	bool isClosed() const {
		return closed;
	}

	BObjSharedPtr read(std::istream& is);

	BObjSharedPtr macroExpand(BObjSharedPtr objPtr);

	BObjSharedPtr procSpecialForm(BObjSharedPtr objPtr, bool tail = false);

	BObjSharedPtr eval(BObjSharedPtr objPtr, bool tail = false);

	BObjSharedPtr evalTop(BObjSharedPtr objPtr) {
		return eval(macroExpand(objPtr));
	}

	void repl() {
		while (1) {
			std::cout << ">> ";
			BObjSharedPtr o = read(std::cin);
			if (o == nullptr) {
				std::cout << std::endl << "Parse failed." << std::endl;
				return;
			}
			o = evalTop(o);
			o->operator<<(std::cout);
			std::cout << std::endl;
			if (o == registerSymbol("exit")) break;
		}
	}

	void print() const {
		std::cout << "{";
		for (auto& kv : symbolValueMap) {
			kv.first->operator<<(std::cout);
			std::cout << ":";
			kv.second->operator<<(std::cout);
			std::cout << ",";
		}
		std::cout << "}";
	}

	void printAll(bool exceptRoot = false) const {
		if (exceptRoot && outEnvironment == nullptr) {
			std::cout << "{...}";
			return;
		}
		std::cout << "{";
		for (auto& kv : symbolValueMap) {
			kv.first->operator<<(std::cout);
			std::cout << ":";
			kv.second->operator<<(std::cout);
			std::cout << ",";
		}
		if (environmentLex != nullptr) {
			std::cout << "#lex:";
			environmentLex->printAll(exceptRoot);
		}
		if (outEnvironment != nullptr) {
			std::cout << "#outer:";
			outEnvironment->printAll(exceptRoot);
		}
		std::cout << "}";
	}
};