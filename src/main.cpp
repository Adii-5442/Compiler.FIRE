#include <iostream>
#include <fstream>
#include <sstream>
#include <optional>
#include <vector>
#include "parser.h"
#include "tokenization.h"
#include "generator.h"

using namespace std;

int main(int argc,char* argv[]){
    if ( argc != 2) {
        cerr << "Incorrect usage" << endl;
        cerr << "dark <input.fire>" << endl;
        return EXIT_FAILURE;
    }
    string contents;
    {
        stringstream contents_stream;
        fstream input(argv[1],ios::in);
        contents_stream<<input.rdbuf();
        contents = contents_stream.str();
    }
    Tokenizer tokenizer(std::move(contents));
    vector<Token> tokens =  tokenizer.tokenize();

    Parser parser(std::move(tokens));
    optional <NodeExit> tree = parser.parse();

    if (!tree.has_value()){
        cerr<<"Function doesnt exits anything"<<endl;
    }
    Generator generator(tree.value());
    {
        fstream file("out.asm",ios::out);
        file << generator.generate();
    }
    system("nasm -felf64 out.asm");
    system("ld -o out out.o");

    return EXIT_SUCCESS;
}