#include "lisp.hpp"

int main(int argc, char* argv[]) {
	Environment = Env::createEnvironment();
	try {
		Environment->repl();
	}
	catch (char const* e) {
		std::cout << "Exception error: " << e << std::endl;
	}
	return 0;
}