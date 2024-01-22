#include "lisp.hpp"

int totalSym = 0;
std::map<std::string, BObjSharedPtr> sMap;
EnvSPtr Environment;

bool Base_Object::isnull() const {
	return this->typep<Symbol>() && this->getAs<Symbol>().name == "null";
}

BObjSharedPtr registerSymbol(std::string name) {
	auto it = sMap.find(name);
	BObjSharedPtr objPtr;
	if (it == sMap.end()) {
		objPtr = std::make_shared<Symbol>(name);
		sMap[name] = objPtr;
	}
	else {
		objPtr = it->second;
	}
	return objPtr;
}

bool isSymbolChar(const char c) {
	return c != '(' && c != ')' && c != ' ' &&
		c != '\t' && c != '\n' && c != '\r' && c != 0;
}


BObjSharedPtr readList(Env& env, std::istream& is) {
	is >> std::ws;
	if (is.eof()) 
		throw "Parser contains errors";
	char c = is.get();
	if (c == ')') {
		return registerSymbol("null");
	}
	else if (c == '.') {
		BObjSharedPtr cdr = readParse(env, is);
		is >> std::ws;
		if (is.get() != ')') 
			throw "Parser contains errors";
		return cdr;
	}
	else {
		is.unget();
		BObjSharedPtr car = readParse(env, is);
		BObjSharedPtr cdr = readList(env, is);
		return BObjSharedPtr(new Cons(car, cdr));
	}
}

BObjSharedPtr readString(Env& env, std::istream& is) {
	char c = is.get();
	std::stringstream ss;
	while (c != '"') {
		if (c == '\\') {
			switch (c = is.get()) {
			case 'n': c = '\n'; break;
			case 'f': c = '\f'; break;
			case 'b': c = '\b'; break;
			case 'r': c = '\r'; break;
			case 't': c = '\t'; break;
			case '\'': c = '\''; break;
			case '\"': c = '\"'; break;
			case '\\': c = '\\'; break;
			case '\n': case '\r': c = 0; break;
			}
		}
		if (c != 0) {
			ss << c;
		}
		if (is.eof())
			throw "Parser contains errors";
		c = is.get();
	}
	return BObjSharedPtr(new String(ss.str()));
}

void commentSkip(std::istream& is) {
	is >> std::ws;
	char c = is.peek();
	while (c == ';') {
		while (c != 0 && c != '\n' && c != '\r') c = is.get();
		is >> std::ws;
		c = is.peek();
	}
}

BObjSharedPtr readParse(Env& env, std::istream& is) {
	commentSkip(is);
	if (is.eof()) throw "Parser contains errors";
	char c = is.get();
	if (c == '(') {
		return readList(env, is);
	}
	else if (('0' <= c && c <= '9') ||
		(c == '-' && ('0' <= is.peek() && is.peek() <= '9'))) {
		is.unget();
		int value;
		is >> value;
		return BObjSharedPtr(new Integer(value));
	}
	else if (c == '"') {
		return readString(env, is);
	}
	else {
		char symbolName[512];
		int i = 0;
		while (isSymbolChar(c) && !is.eof()) {
			symbolName[i++] = c;
			c = is.get();
		}
		is.unget();
		if (i == 0) throw "Parser contains errors";
		symbolName[i] = 0;
		return registerSymbol(symbolName);
	}
}

BObjSharedPtr Env::read(std::istream& is) {
	try {
		return readParse(*this, is);
	}
	catch (char const* e) {
		return BObjSharedPtr(nullptr);
	}
}

BObjSharedPtr listLastCdrObj(BObjSharedPtr objPtr) {
	if (objPtr->typep<Cons>())
		return listLastCdrObj(objPtr->getAs<Cons>().cdr);
	return objPtr;
}

bool isProperList(Base_Object* obj) {
	if (typeid(*obj) == typeid(Cons))
		return isProperList(dynamic_cast<Cons*>(obj)->cdr.get());
	if (typeid(*obj) == typeid(Symbol))
		return dynamic_cast<Symbol*>(obj)->name == "null";
	return false;
}

int listLength(Base_Object* obj) {
	if (typeid(*obj) == typeid(Cons))
		return 1 + listLength(dynamic_cast<Cons*>(obj)->cdr.get());
	return 0;
}

BObjSharedPtr listNth(BObjSharedPtr& objptr, int i) {
	if (typeid(*objptr) != typeid(Cons))
		return BObjSharedPtr(nullptr);
	if (i == 0)
		return dynamic_cast<Cons*>(objptr.get())->car;
	return listNth(dynamic_cast<Cons*>(objptr.get())->cdr, i - 1);
}

BObjSharedPtr listNthCdr(BObjSharedPtr& objptr, int i) {
	if (i == 0)
		return objptr;
	if (typeid(*objptr) != typeid(Cons))
		return BObjSharedPtr(nullptr);
	return listNthCdr(dynamic_cast<Cons*>(objptr.get())->cdr, i - 1);
}

BObjSharedPtr map(BObjSharedPtr objPtr, std::function<BObjSharedPtr(BObjSharedPtr)> func) {
	if (typeid(*objPtr) != typeid(Cons))
		return objPtr;
	Cons* cons = dynamic_cast<Cons*>(objPtr.get());
	return std::make_shared<Cons>(func(cons->car), map(cons->cdr, func));
}

BObjSharedPtr boolToLobj(bool b) {
	return registerSymbol(b ? "t" : "f");
}

BObjSharedPtr evalListElements(EnvSPtr env, BObjSharedPtr objPtr) {
	if (typeid(*objPtr) != typeid(Cons)) return objPtr;
	Cons* cons = dynamic_cast<Cons*>(objPtr.get());
	return std::make_shared<Cons>(env->eval(cons->car), evalListElements(env, cons->cdr));
}

BObjSharedPtr vectorToList(std::vector<BObjSharedPtr>& v) {
	BObjSharedPtr list = registerSymbol("null");
	for (auto it = v.rbegin(); it != v.rend(); ++it) {
		list = std::make_shared<Cons>(*it, list);
	}
	return list;
}

EnvSPtr makeEnvForMacro(EnvSPtr outEnvironment, EnvSPtr procEnv, BObjSharedPtr prms, BObjSharedPtr args, bool tail = false) {
	EnvSPtr env = outEnvironment->createSubEnvironment(procEnv);
	if (!isProperList(args.get()))
		throw "Wrong usage of macro";
	while (prms->typep<Cons>() && args->typep<Cons>()) {
		Symbol* symbol = &prms->getAs<Cons>().car->getAs<Symbol>();
		env->bind(args->getAs<Cons>().car, symbol);
		prms = prms->getAs<Cons>().cdr;
		args = args->getAs<Cons>().cdr;
	}
	if (typeid(*prms) == typeid(Symbol) && !prms->isnull()) {
		env->bind(args, &prms->getAs<Symbol>());
	}
	if (tail && !outEnvironment->isClosed()) {
		outEnvironment->merge(env);
		env = outEnvironment;
	}
	return env;
}

EnvSPtr makeEnvForApply(EnvSPtr outEnvironment, EnvSPtr procEnv, BObjSharedPtr prms, BObjSharedPtr args, bool tail = false) {
	EnvSPtr env = outEnvironment->createSubEnvironment(procEnv);
	if (!isProperList(args.get()))
		throw "Wrong usage";
	while (prms->typep<Cons>() && args->typep<Cons>()) {
		Symbol* symbol = &prms->getAs<Cons>().car->getAs<Symbol>();
		env->bind(outEnvironment->eval(args->getAs<Cons>().car), symbol);
		prms = prms->getAs<Cons>().cdr;
		args = args->getAs<Cons>().cdr;
	}
	if (typeid(*prms) == typeid(Symbol) && !prms->isnull()) {
		BObjSharedPtr rest = evalListElements(outEnvironment, args);
		env->bind(rest, &prms->getAs<Symbol>());
	}
	if (tail && !outEnvironment->isClosed()) {
		outEnvironment->merge(env);
		env = outEnvironment;
	}
	return env;
}


Env::Env() {
	BObjSharedPtr obj;
	PredefinedProc* bfunc;

	obj = registerSymbol("t");
	bind(obj, &obj->getAs<Symbol>());

	obj = registerSymbol("null");
	bind(obj, &obj->getAs<Symbol>());

	obj = registerSymbol("eq?");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() == 0) throw "Invalid arguments of function 'eq?'";
		for (int i = 0; i < args.size() - 1; ++i) {
			if (!(args[i].get() == args[i + 1].get()))
				return registerSymbol("f");
		}
		return registerSymbol("t");
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("null?");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 1) throw "Invalid arguments of function 'null'";
		return boolToLobj(args[0]->isnull());
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("cons?");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 1) throw "Invalid arguments of function 'null'";
		return boolToLobj(typeid(*args[0]) == typeid(Cons));
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("list?");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 1) throw "Invalid arguments of function 'null'";
		return boolToLobj(typeid(*args[0]) == typeid(Cons) || args[0]->isnull());
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("symbol?");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 1) throw "Invalid arguments of function 'null'";
		return boolToLobj(typeid(*args[0]) == typeid(Symbol));
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("int?");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 1) throw "Invalid arguments of function 'null'";
		return boolToLobj(typeid(*args[0]) == typeid(Integer));
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("string?");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 1) throw "Invalid arguments of function 'null'";
		return boolToLobj(typeid(*args[0]) == typeid(String));
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("proc?");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 1) throw "Invalid arguments of function 'null'";
		return boolToLobj(typeid(*args[0]) == typeid(Proc) ||
			typeid(*args[0]) == typeid(PredefinedProc));
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("+");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		int value = 0;
		for (BObjSharedPtr& objPtr : args) {
			if (typeid(*objPtr) != typeid(Integer)) throw "Invalid arguments of function '+'";
			value += dynamic_cast<Integer*>(objPtr.get())->value;
		}
		return std::make_shared<Integer>(value);
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("-");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() == 0 || typeid(*args[0]) != typeid(Integer))
			throw "Invalid arguments of function '-'";
		int value = dynamic_cast<Integer*>(args[0].get())->value;
		if (args.size() == 1)
			return std::make_shared<Integer>(-value);
		for (int i = 1; i < args.size(); ++i) {
			if (typeid(*args[i]) != typeid(Integer)) throw "Invalid arguments of function '-'";
			value -= dynamic_cast<Integer*>(args[i].get())->value;
		}
		return std::make_shared<Integer>(value);
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("*");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		int value = 1;
		for (BObjSharedPtr& objPtr : args) {
			if (typeid(*objPtr) != typeid(Integer)) throw "Invalid arguments of function '*'";
			value *= dynamic_cast<Integer*>(objPtr.get())->value;
		}
		return std::make_shared<Integer>(value);
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("/");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() == 0 || typeid(*args[0]) != typeid(Integer))
			throw "Invalid arguments of function '/'";
		int value = dynamic_cast<Integer*>(args[0].get())->value;
		for (int i = 1; i < args.size(); ++i) {
			if (typeid(*args[i]) != typeid(Integer)) throw "Invalid arguments of function '/'";
			int divisor = dynamic_cast<Integer*>(args[i].get())->value;
			if (divisor == 0) throw "dividing by zero";
			value /= divisor;
		}
		return std::make_shared<Integer>(value);
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("mod");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 2 ||
			typeid(*args[0]) != typeid(Integer) || typeid(*args[1]) != typeid(Integer))
			throw "Invalid arguments of function 'mod'";
		int value = dynamic_cast<Integer*>(args[0].get())->value;
		int divisor = dynamic_cast<Integer*>(args[1].get())->value;
		if (divisor == 0) throw "dividing by zero";
		return std::make_shared<Integer>(value % divisor);
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("=");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() == 0) throw "Invalid arguments of function '='";
		for (BObjSharedPtr& objPtr : args) {
			if (typeid(*objPtr) != typeid(Integer)) throw "Invalid arguments of function '='";
		}
		for (int i = 0; i < args.size() - 1; ++i) {
			if (dynamic_cast<Integer*>(args[i].get())->value !=
				dynamic_cast<Integer*>(args[i + 1].get())->value)
				return registerSymbol("null");
		}
		return registerSymbol("t");
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("<");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() == 0) throw "Invalid arguments of function '<'";
		for (BObjSharedPtr& objPtr : args) {
			if (typeid(*objPtr) != typeid(Integer)) throw "Invalid arguments of function '<'";
		}
		for (int i = 0; i < args.size() - 1; ++i) {
			if (dynamic_cast<Integer*>(args[i].get())->value >=
				dynamic_cast<Integer*>(args[i + 1].get())->value)
				return registerSymbol("null");
		}
		return registerSymbol("t");
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("print");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		for (BObjSharedPtr& objPtr : args) {
			objPtr->operator<<(std::cout);
		}
		return registerSymbol("null");
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("println");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		for (BObjSharedPtr& objPtr : args) {
			objPtr->operator<<(std::cout);
			std::cout << std::endl;
		}
		return registerSymbol("null");
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("print-to-string");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		std::stringstream ss;
		for (BObjSharedPtr& objPtr : args) {
			objPtr->operator<<(ss);
		}
		return std::make_shared<String>(ss.str());
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("car");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 1 || typeid(*args[0]) != typeid(Cons))
			throw "Invalid arguments of function 'car'";
		return dynamic_cast<Cons*>(args[0].get())->car;
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("cdr");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 1 || typeid(*args[0]) != typeid(Cons))
			throw "Invalid arguments of function 'cdr'";
		return dynamic_cast<Cons*>(args[0].get())->cdr;
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("cons");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 2)
			throw "Invalid arguments of function 'cons'";
		return std::make_shared<Cons>(args[0], args[1]);
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("gensym");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		std::stringstream ss;
		if (args.size() == 0) {
			ss << "#g" << (totalSym++);
		}
		else if (args.size() == 1 && typeid(*args[0]) == typeid(String)) {
			ss << "#" << (static_cast<String*>(args[0].get())->value) << (totalSym++);
		}
		else {
			throw "Invalid arguments of function 'gensym'";
		}
		return std::make_shared<Symbol>(ss.str());
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("bound?");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 1 || !args[0]->typep<Symbol>())
			throw "Invalid arguments of function 'bound?'";
		return boolToLobj(env.findSymbolInMap(&args[0]->getAs<Symbol>()) != nullptr);
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("get-time");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 0)
			throw "Invalid arguments of function 'get-time'";
		return std::make_shared<Integer>(static_cast<int>(std::clock() / (CLOCKS_PER_SEC / 1000)));
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("eval");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 1)
			throw "Invalid arguments of function 'eval'";
		return env.evalTop(args[0]);
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("read");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 0)
			throw "Invalid arguments of function 'read'";
		return env.read(std::cin);
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("load");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 1 || typeid(*args[0]) != typeid(String))
			throw "Invalid arguments of function 'load'";
		std::string filename = dynamic_cast<String*>(args[0].get())->value;
		std::ifstream ifs(filename);
		if (ifs.fail()) return registerSymbol("null");
		try {
			while (!ifs.eof()) {
				BObjSharedPtr o = env.read(ifs);
				env.evalTop(o);
				commentSkip(ifs);
			}
		}
		catch (char const* e) {
			std::cout << std::endl << "Parser contains errors." << std::endl;
			return registerSymbol("null");
		}
		return registerSymbol("t");
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("macroexpand-all");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 1)
			throw "Invalid arguments of function 'macroexpand-all'";
		return env.macroExpand(args[0]);
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("exit");
	bind(obj, &obj->getAs<Symbol>());

	obj = registerSymbol("env-print");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 0)
			throw "Invalid arguments of function 'env-print'";
		env.print();
		std::cout << std::endl;
		return registerSymbol("null");
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());

	obj = registerSymbol("env-print-all");
	bfunc = new PredefinedProc([](Env& env, std::vector<BObjSharedPtr>& args) {
		if (args.size() != 0)
			throw "Invalid arguments of function 'env-print-all'";
		env.printAll(true);
		std::cout << std::endl;
		return registerSymbol("null");
		});
	bind(BObjSharedPtr(bfunc), &obj->getAs<Symbol>());
}

BObjSharedPtr Env::macroExpand(BObjSharedPtr objPtr) {
	if (!objPtr->typep<Cons>())
		return objPtr;
	Cons* cons = &objPtr->getAs<Cons>();
	if (cons->car->typep<Symbol>()) {
		Symbol* opSymbol = &cons->car->getAs<Symbol>();
		if (opSymbol->name == "quote") {
			return objPtr;
		}
		BObjSharedPtr op = findSymbolInMap(opSymbol);
		if (op != nullptr && op->typep<Macro>()) {
			Macro* macro = &op->getAs<Macro>();
			EnvSPtr env = makeEnvForMacro(EnvSPtr(envobj), macro->env,
				macro->parameterList, cons->cdr);
			return macroExpand(env->eval(macro->body));
		}
	}
	return map(objPtr, [this](BObjSharedPtr objPtr) {
		return this->macroExpand(objPtr);
		});
}

BObjSharedPtr Env::procSpecialForm(BObjSharedPtr objPtr, bool tail) {
	Cons* cons = &objPtr->getAs<Cons>();
	Base_Object* op = cons->car.get();
	if (!op->typep<Symbol>())
		return BObjSharedPtr(nullptr);

	int length = listLength(cons);
	std::string operand = op->getAs<Symbol>().name;
	if (operand == "if") {
		if (length == 3 || length == 4) {
			BObjSharedPtr cond = listNth(objPtr, 1);
			if (!eval(cond)->isnull()) {
				return eval(listNth(objPtr, 2), tail);
			}
			else if (length == 4) {
				return eval(listNth(objPtr, 3), tail);
			}
			else {
				return registerSymbol("null");
			}
		}
	}
	else if (operand == "quote") {
		if (length == 2)
			return listNth(objPtr, 1);
	}
	else if (operand == "do") {
		if (length == 1)
			return registerSymbol("null");
		cons = &cons->cdr->getAs<Cons>();
		while (cons->cdr->typep<Cons>()) {
			eval(cons->car);
			cons = &cons->cdr->getAs<Cons>();
		}
		return eval(cons->car, tail);
	}
	else if (operand == "define") {
		if (length == 3) {
			BObjSharedPtr variable = listNth(objPtr, 1);
			if (typeid(*variable) != typeid(Symbol))
				throw "Wrong 'define'";
			Symbol* symbol = dynamic_cast<Symbol*>(variable.get());
			EnvSPtr env = Environment;
			env->bind(eval(listNth(objPtr, 2), tail), symbol);
			return variable;
		}
	}
	else if (operand == "set!") {
		if (length == 3) {
			BObjSharedPtr variable = listNth(objPtr, 1);
			if (typeid(*variable) != typeid(Symbol))
				throw "Wrong 'set!'";
			Symbol* symbol = dynamic_cast<Symbol*>(variable.get());
			EnvSPtr env = findEnvironment(symbol);
			if (env == nullptr) env = Environment;
			BObjSharedPtr value = eval(listNth(objPtr, 2), tail);
			env->bind(value, symbol);
			return value;
		}
	}
	else if (operand == "let") {
		if (length < 2) throw "Wrong usage";
		BObjSharedPtr bindings = listNth(objPtr, 1);
		if (!isProperList(bindings.get())) 
			throw "Wrong let bindings";
		if (listLength(bindings.get()) % 2 != 0) 
			throw "Odd number of let bindings";
		EnvSPtr env = createSubEnvironment();
		while (!bindings->isnull()) {
			BObjSharedPtr objSymbol = dynamic_cast<Cons*>(bindings.get())->car;
			BObjSharedPtr objForm = dynamic_cast<Cons*>(dynamic_cast<Cons*>(bindings.get())->cdr.get())->car;
			env->bind(eval(objForm), &objSymbol->getAs<Symbol>());
			bindings = listNthCdr(bindings, 2);
		}
		if (tail && !closed) {
			this->merge(env);
			env = EnvSPtr(envobj);
		}
		return env->eval(std::make_shared<Cons>(registerSymbol("do"), listNthCdr(objPtr, 2)), TailCallOptimisation);
	}
	else if (operand == "let*") {
		if (length < 2) throw 
			"Wrong 'let*'";

		BObjSharedPtr bindings = listNth(objPtr, 1);
		if (!isProperList(bindings.get())) throw 
			"bad let* bindings";
		if (listLength(bindings.get()) % 2 != 0) throw 
			"number of bindings elements of let* is odd";
		EnvSPtr env;
		if (tail && !closed) {
			env = EnvSPtr(envobj);
		}
		else {
			env = createSubEnvironment();
		}
		while (!bindings->isnull()) {
			BObjSharedPtr objSymbol = dynamic_cast<Cons*>(bindings.get())->car;
			BObjSharedPtr objForm = dynamic_cast<Cons*>(dynamic_cast<Cons*>(bindings.get())->cdr.get())->car;
			Symbol* symbol = dynamic_cast<Symbol*>(objSymbol.get());
			env->bind(env->eval(objForm), symbol);
			bindings = listNthCdr(bindings, 2);
		}
		return env->eval(std::make_shared<Cons>(registerSymbol("do"), listNthCdr(objPtr, 2)), TailCallOptimisation);
	}
	else if (operand == "lambda") {
		if (2 <= length) {
			BObjSharedPtr pl = listNth(objPtr, 1);
			closed = true;
			return std::make_shared<Proc>(pl, std::make_shared<Cons>(registerSymbol("do"), listNthCdr(objPtr, 2)), EnvSPtr(envobj));
		}
	}
	else if (operand == "macro") {
		if (2 <= length) {
			BObjSharedPtr pl = listNth(objPtr, 1);
			closed = true;
			return std::make_shared<Macro>(pl, std::make_shared<Cons>(registerSymbol("do"), listNthCdr(objPtr, 2)), EnvSPtr(envobj));
		}
	}
	return BObjSharedPtr(nullptr);
}

BObjSharedPtr Env::eval(BObjSharedPtr objPtr, bool tail) {
	Base_Object* B_o = objPtr.get();
	if (B_o->typep<Symbol>()) {
		BObjSharedPtr rr = findSymbolInMap(&B_o->getAs<Symbol>());
		if (rr == nullptr) {
			std::cout << "Unresolvable symbol: " << B_o->getAs<Symbol>().name << std::endl;
			throw "Evaluated unresolvable symbol";
		}
		return rr;
	}
	if (B_o->typep<Integer>() || B_o->typep<String>()) {
		return objPtr;
	}
	if (B_o->typep<Cons>()) {
		BObjSharedPtr psfr = procSpecialForm(objPtr, tail);
		if (psfr != nullptr) {
			return psfr;
		}

		Cons* cons = &B_o->getAs<Cons>();
		BObjSharedPtr opPtr = eval(cons->car);
		if (opPtr->typep<Proc>()) {
			Proc* func = &opPtr->getAs<Proc>();
			EnvSPtr env = makeEnvForApply(EnvSPtr(envobj), func->env,
				func->parameterList, cons->cdr, tail);
			return env->eval(func->body, TailCallOptimisation);
		}

		if (opPtr->typep<PredefinedProc>()) {
			PredefinedProc* bfunc = &opPtr->getAs<PredefinedProc>();
			Base_Object* argCons = cons->cdr.get();
			if (!isProperList(argCons))
				throw "Wrong usage of Predefined Function";
			std::vector<BObjSharedPtr> args;
			while (!argCons->isnull()) {
				args.push_back(eval(argCons->getAs<Cons>().car));
				argCons = argCons->getAs<Cons>().cdr.get();
			}
			return bfunc->function(*this, args);
		}
		throw "Wrong usage";
	}
	return objPtr;
}

