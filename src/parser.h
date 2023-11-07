#pragma once
#include <vector>
#include <string>
#include "tokenization.h"

struct NodeExpr{
    Token int_lit;
};

struct NodeExit{
    NodeExpr expr;
};

using namespace std;

class Parser {
public:
    inline explicit  Parser(vector<Token> tokens)
        :m_tokens(move(tokens))
    {

    }
    optional<NodeExpr> parse_expr(){
        if(peek().has_value() && peek().value().type == TokenType::int_lit){
            return NodeExpr{.int_lit = consume()};
        }
        else{
            return {};
        }
    }
    optional<NodeExit> parse() {
        optional<NodeExit> exit_node;
        while(peek().has_value()){
            if(peek().value().type == TokenType::exit && peek(1).has_value() && peek(1).value().type == TokenType::open_paren){
                consume();
                consume();
                if(auto node_expr = parse_expr()){
                    exit_node = NodeExit {.expr = node_expr.value()};
                }else{
                    cerr<<"Invalid Expression"<<endl;
                    exit(EXIT_FAILURE);
                }
                if(peek().has_value() && peek().value().type == TokenType::close_paren){
                    consume();
                }else{
                    cerr<<"Expected ')'"<<endl;
                    exit(EXIT_FAILURE);
                }
                if (peek().has_value() && peek().value().type == TokenType::semi){
                    consume();
                }else{
                    cerr<<"Expected ';'"<<endl;
                    exit(EXIT_FAILURE);
                }
            }
        }
        m_index =0;
        return exit_node;
    }
private:
    [[nodiscard]] inline optional<Token> peek(int offset =0) const
    {
        if(m_index + offset >= m_tokens.size()){
            return {};
        }else{
            return m_tokens.at(m_index + offset);
        }
    }

    inline Token consume(){
        return m_tokens.at(m_index++);
    }
    const vector<Token> m_tokens;
    size_t m_index = 0;
};