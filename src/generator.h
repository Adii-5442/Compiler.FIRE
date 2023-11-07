#pragma once
#include "parser.h"
#include <sstream>
using namespace std;

class Generator {
public:
    inline Generator(NodeExit root) :m_root(move(root))
    {

    }

    [[nodiscard]] string generate() const
    {
        stringstream output;
        output << "global _start\n_start:\n";
        output <<" mov rax, 60\n";
        output<< " mov rdi," << m_root.expr.int_lit.value.value()<<"\n";
        output<< " syscall";

        return output.str();
    }
private:
    const NodeExit m_root;

};