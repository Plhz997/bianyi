#include "frontend/Lexer.h"
#include "frontend/Parser.h"
#include "frontend/Sema.h"
#include "ir/IRGen.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace by;

namespace {

struct Options {
  std::string input;
  std::string output;
  bool emitAssembly = false;
  bool dumpAst = false;
  bool dumpIr = false;
  bool optimize = false;
};

void printUsage(const char *argv0) {
  std::cerr << "usage: " << argv0
            << " testcase.sysy [-S -o testcase.s] [-O1] [--dump-ast]"
               " [--dump-ir]\n";
}

bool parseArgs(int argc, char **argv, Options &opts) {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-S") {
      opts.emitAssembly = true;
    } else if (arg == "-o") {
      if (i + 1 >= argc) {
        std::cerr << "missing output path after -o\n";
        return false;
      }
      opts.output = argv[++i];
    } else if (arg == "-O1") {
      opts.optimize = true;
    } else if (arg == "--dump-ast") {
      opts.dumpAst = true;
    } else if (arg == "--dump-ir") {
      opts.dumpIr = true;
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "unknown option: " << arg << '\n';
      return false;
    } else if (opts.input.empty()) {
      opts.input = arg;
    } else {
      std::cerr << "unexpected argument: " << arg << '\n';
      return false;
    }
  }

  if (opts.input.empty()) {
    std::cerr << "missing input file\n";
    return false;
  }
  if (opts.emitAssembly && opts.output.empty()) {
    std::cerr << "missing output file for -S\n";
    return false;
  }
  return true;
}

bool readFile(const std::string &path, std::string &content) {
  std::ifstream input(path);
  if (!input) {
    std::cerr << "cannot open input file: " << path << '\n';
    return false;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  content = buffer.str();
  return true;
}

bool writeFrontendPlaceholderAssembly(const std::string &path,
                                      bool optimizeEnabled) {
  std::ofstream output(path);
  if (!output) {
    std::cerr << "cannot open output file: " << path << '\n';
    return false;
  }

  output << "\t.text\n";
  output << "\t.globl main\n";
  output << "\t.type main, @function\n";
  output << "main:\n";
  output << "\t# frontend milestone placeholder";
  if (optimizeEnabled) {
    output << " (-O1 accepted)";
  }
  output << "\n";
  output << "\tli a0, 0\n";
  output << "\tret\n";
  output << "\t.size main, .-main\n";
  return true;
}

} // namespace

int main(int argc, char **argv) {
  Options opts;
  if (!parseArgs(argc, argv, opts)) {
    printUsage(argv[0]);
    return 1;
  }

  std::string source;
  if (!readFile(opts.input, source)) {
    return 1;
  }

  DiagnosticEngine diag;
  Lexer lexer(opts.input, source, diag);
  auto tokens = lexer.tokenize();

  Parser parser(std::move(tokens), diag);
  auto unit = parser.parse();

  SemanticAnalyzer sema(diag);
  sema.analyze(*unit);

  if (diag.hasErrors()) {
    diag.printAll();
    return 1;
  }

  if (opts.dumpAst) {
    unit->dump(std::cout, 0);
  }

  ir::IRGenerator irgen;
  auto module = irgen.generate(*unit);

  if (opts.dumpIr) {
    module.dump(std::cout);
  }

  if (opts.emitAssembly) {
    if (!writeFrontendPlaceholderAssembly(opts.output, opts.optimize)) {
      return 1;
    }
  }

  return 0;
}
