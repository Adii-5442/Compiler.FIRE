#pragma once    // : used to define that this module is going to be used / get initialised only once (even if we use its components multiple times)

#include <string>
#include <vector>

using namespace std;

enum class TokenType{
    exit,
    int_lit,
    semi,
    open_paren,
    close_paren,
};

struct Token {
    TokenType type;
    optional<string> value {};

};


class Tokenizer {
public:
    inline explicit  Tokenizer(string src)
        : m_src(move(src))
    {

    }

    inline vector<Token> tokenize()
    {
        vector<Token> tokens;
        string buf;

        while(peek().has_value()){
            if(isalpha(peek().value())){
                buf.push_back(consume());
                while(peek().has_value() && isalnum(peek().value())){
                    buf.push_back(consume());
                }
                if(buf=="exit"){
                    tokens.push_back({.type = TokenType::exit});
                    buf.clear();
                    continue;

                }else{
                    cerr<<"You messed up finally !"<<endl;
                    exit(EXIT_FAILURE);

                }
            }
            else if(isdigit(peek().value())){
                buf.push_back(consume());
                while(peek().has_value() && isdigit(peek().value())){
                    buf.push_back(consume());
                }
                tokens.push_back({.type=TokenType::int_lit, .value = buf});
                buf.clear();
            }
            else if(peek().value() == '('){
                consume();
                tokens.push_back({.type=TokenType::open_paren});
            }
            else if(peek().value() == ')'){
                consume();
                tokens.push_back({.type=TokenType::close_paren});
            }

            else if(peek().value() == ';'){
                consume();
                tokens.push_back({.type=TokenType::semi});
                continue;
            }
            else if (isspace(peek().value())){
                consume();
                continue;
            }else{
                cerr<<"You messed up finally !"<<endl;
                exit(EXIT_FAILURE);
            }
        }
        m_index=0;
        return tokens;

    }


private:
    [[nodiscard]] inline optional<char> peek(int offset =0) const
    {
        if(m_index + offset >= m_src.length()){
            return {};
        }else{
            return m_src.at(m_index + offset);
        }
    }

    inline char consume(){
        return m_src.at(m_index++);
    }


    const string m_src;
    size_t m_index=0;

};