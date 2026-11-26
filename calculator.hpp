#pragma once
#include "terminal.hpp"
#include <cmath>

namespace calculator {
struct Cancelled {};
class Expression {
    string text;
    size_t pos=0;
    void space() {while(pos<text.size() && text[pos]==' ') ++pos;}
    bool take(char c) {space();if(pos<text.size() && text[pos]==c){++pos;return true;}return false;}
    long double value() {
        if(take('(')) {
            auto n=sum();
            if(!take(')')) throw std::invalid_argument("Close the bracket with ).");
            return n;
        }
        if(take('-')) return -value();
        if(take('+')) return value();
        space();
        auto start=pos;
        while(pos<text.size() && (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos]=='.')) ++pos;
        auto token=text.substr(start,pos-start);
        // Reuse wallet amount validation for each numeric operand.
        return static_cast<long double>(InputTools::parseAmount(token).raw())/100;
    }
    long double product() {
        auto n=value();
        for(;;) {
            if(take('*')) n*=value();
            else if(take('/')) {
                auto divisor=value();
                if(divisor==0) throw std::invalid_argument("Cannot divide by zero.");
                n/=divisor;
            } else return n;
        }
    }
    long double sum() {
        auto n=product();
        for(;;) {
            if(take('+')) n+=product();
            else if(take('-')) n-=product();
            else return n;
        }
    }
public:
    explicit Expression(string input):text(std::move(input)) {}
    Money evaluate() {
        if(text.size()>200) throw std::invalid_argument("Use an expression of up to 200 characters.");
        auto result=sum();
        space();
        if(pos!=text.size()) throw std::invalid_argument("Use numbers, + - * / and brackets.");
        if(!std::isfinite(result) || result<=0 || result>999999999999.99L)
            throw std::invalid_argument("Result must be positive and within the amount limit.");
        auto paise=static_cast<Cents>(std::round(result*100));
        if(!paise) throw std::invalid_argument("Result rounds to zero. Enter at least 0.01.");
        return Money(paise);
    }
};
inline Money input(const string& title) {
    string message;
    for(;;) {
        TerminalUI::screen(title,24);
        int width=std::min(TerminalUI::contentWidth(),48);
        vector<string> rows{
            "Amount or calculation · INR",
            "Example  (120 + 80) / 2",
            "› ",
            "",
            "", "", "", "", "", "", "", "", "",
            "",
            "Type + - * / ( ) · B to return"
        };
        if(width>=37) {
            const string keys[]{"789/","456*","123-",".0(+"};
            for(int row=0;row<9;++row) {
                bool keyRow=row%2==1;
                string line=keyRow?"│":row==0?"╭":row==8?"╰":"├";
                for(int col=0;col<4;++col) {
                    int cell=(width-9)/4+(col<(width-9)%4?1:0);
                    if(keyRow) {
                        int left=(cell-1)/2;
                        line+=string(left,' ')+keys[row/2][col]+string(cell-left-1,' ');
                    } else line+=TerminalUI::repeat("─",cell);
                    line+=keyRow?"│":row==0?(col==3?"╮":"┬"):row==8?(col==3?"╯":"┴"):(col==3?"┤":"┼");
                }
                rows[4+row]=line;
            }
        }
        if(width<37) rows={"Amount · INR","Type an expression","› ","","7  8  9  /","4  5  6  *","1  2  3  -",".  0  (  +","Enter · B to return"};
        auto border=[&](const string& left,const string& right) {
            std::cout<<TerminalUI::inset(width)<<TerminalUI::paint(left+TerminalUI::repeat("─",width-2)+right,"90")<<'\n';
        };
        border("╭","╮");
        for(size_t i=0;i<rows.size();++i) {
            string value=rows[i];
            std::cout<<TerminalUI::inset(width)<<TerminalUI::paint("│ ","90")
                <<TerminalUI::paint(TerminalUI::fit(value,width-4),i==0?"36":i>=4 && i<=12?"34":i==1 || i==rows.size()-1?"37":"97")
                <<TerminalUI::paint(" │","90")<<'\n';
        }
        border("╰","╯");
        if(!message.empty()) std::cout<<TerminalUI::inset(width)<<TerminalUI::paint(TerminalUI::fit(message,width),"33")<<'\n';
        try {
            string expression;
            if(colorEnabled) {
                std::cout<<"\033[s\033["<<rows.size()-1+(!message.empty()?1:0)<<"A\r"<<TerminalUI::inset(width)<<"\033[4C"<<std::flush;
                if(!std::getline(std::cin,expression)) throw std::ios_base::failure("Input closed");
                std::cout<<"\033[u";
            } else expression=InputTools::prompt("Amount or calculation");
            if(InputTools::lower(InputTools::trim(expression))=="b") throw Cancelled{};
            auto amount=Expression(expression).evaluate();
            TerminalUI::cards({{"Calculated amount",{TerminalUI::money(amount),"Enter to use · type C to change"},"32"}},width);
            auto choice=InputTools::lower(InputTools::prompt("Confirm",true));
            if(choice.empty()) return amount;
            if(choice=="b") throw Cancelled{};
            message=choice=="c"?"":"Press Enter to use, C to change, or B to return";
        } catch(const std::invalid_argument& error) {message=error.what();}
    }
}
}
