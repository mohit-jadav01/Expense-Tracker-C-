#pragma once
#include "common.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

// Shared terminal renderer. Static methods need no financial state.
class TerminalUI {
public:
static std::pair<int,int> viewport() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if(GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE),&info))
        return {std::max(12,int(info.srWindow.Right-info.srWindow.Left)),int(info.srWindow.Bottom-info.srWindow.Top+1)};
#else
    winsize size{};
    if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&size)==0 && size.ws_col)
        return {std::max(12,int(size.ws_col)-1),int(size.ws_row)};
#endif
    return {79,30};
}
// Terminal columns differ from the UTF-8 byte count (₹ is three bytes).
static int nextGlyph(const string& s,size_t& pos) {
    unsigned cp=static_cast<unsigned char>(s[pos++]);
    if(cp<128) return 1;
    int extra=cp<224?1:cp<240?2:3;
    cp &= (1u<<(6-extra))-1;
    while(extra-- && pos<s.size() && (static_cast<unsigned char>(s[pos])&192)==128)
        cp=(cp<<6)|(static_cast<unsigned char>(s[pos++])&63);
    if((cp>=0x300 && cp<=0x36f) || cp==0x200d || (cp>=0xfe00 && cp<=0xfe0f)) return 0;
    return ((cp>=0x2e80 && cp<=0xa4cf)||(cp>=0xac00 && cp<=0xd7a3)||
            (cp>=0xff01 && cp<=0xff60)||(cp>=0x1f300 && cp<=0x1faff))?2:1;
}
static int columns(const string& s) {
    int n=0; for(size_t i=0;i<s.size();) n+=nextGlyph(s,i); return n;
}
static size_t prefix(const string& s,int limit) {
    size_t i=0;
    while(i<s.size()) {
        size_t before=i; limit-=nextGlyph(s,i); if(limit<0) return before;
    }
    return i;
}
static string fit(string text,int width,bool right=false) {
    if(width<=0) return "";
    if(columns(text)>width) text=text.substr(0,prefix(text,width-1))+"…";
    string pad(width-columns(text),' ');
    return right?pad+text:text+pad;
}
static string money(Money value) {
    string s=value.str();
    if(s.front()=='-') s.erase(0,1);
    for(int i=int(s.find('.'))-3;i>0;i-=2) s.insert(i,",");
    return (value.raw()<0?"-₹":"₹")+s;
}
static string shortDate(const string& iso) {
    static const char* names[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    return std::to_string(std::stoi(iso.substr(8,2)))+" "+names[std::stoi(iso.substr(5,2))-1];
}
static string repeat(const string& glyph,int n) {
    string out; for(int i=0;i<n;++i) out+=glyph; return out;
}
static string paint(const string& text,const string& style) {
    return colorEnabled?"\033["+style+"m"+text+"\033[0m":text;
}
static string meter(double ratio,int width=14) {
    double filled=std::clamp(ratio,0.0,1.0)*width;
    int used=int(filled);
    if(used==width) return repeat("█",width);
    const string partial[]{"░","▏","▎","▍","▌","▋","▊","▉"};
    int fraction=int((filled-used)*8);
    if(filled>0 && used==0 && fraction==0) fraction=1;
    return repeat("█",used)+partial[fraction]+repeat("░",width-used-1);
}
static void clear() {
    if(colorEnabled) std::cout << "\033[2J\033[H";
}
// Keep related amounts together even in a maximized terminal.
struct Card {
    string title;
    vector<string> rows;
    string accent="39";
};
static int contentWidth() { return std::min(144,std::max(12,viewport().first-4)); }
static string inset(int width) {return string(std::max(0,(viewport().first-width)/2),' ');}
static string margin() { return inset(contentWidth()); }
static void screen(const string& name,int height) {
    clear();
    std::cout<<string(std::max(0,(viewport().second-height)/2),'\n');
    string label=fit(name,std::min(columns(name),contentWidth()));
    std::cout<<inset(columns(label))<<paint(label,"1;97")<<"\n";
    int bar=std::min(contentWidth(),std::max(12,columns(label)+6));
    std::cout<<inset(bar)<<paint(repeat("━",bar),"36")<<"\n\n";
}
static vector<string> wrap(string text,int width) {
    vector<string> parts;
    while(columns(text)>width) {
        size_t cut=prefix(text,width),space=text.rfind(' ',cut);
        if(space!=string::npos && space>0) cut=space;
        parts.push_back(text.substr(0,cut));
        text.erase(0,cut+(text[cut]==' '));
    }
    parts.push_back(text);return parts;
}
static void cards(vector<Card> group,int width) {
    if(group.empty()) return;
    int n=int(group.size()), cell=(width-2*(n-1))/n;
    if(n>1 && cell<23) {
        for(const auto& c:group) cards({c},width);
        return;
    }
    for(int i=0;i<n;++i) {
        int inner=cell+(i<(width-2*(n-1))%n?1:0)-4;
        vector<string> full;
        auto title=wrap(group[i].title,inner);
        group[i].title=title.front();
        full.insert(full.end(),title.begin()+1,title.end());
        for(const auto& line:group[i].rows) {
            auto parts=wrap(line,inner);
            full.insert(full.end(),parts.begin(),parts.end());
        }
        group[i].rows=std::move(full);
    }
    size_t rows=0;
    for(const auto& c:group) rows=std::max(rows,c.rows.size());
    const string inset=TerminalUI::inset(width);
    for(int y=-2;y<=int(rows);++y) {
        std::cout<<inset;
        for(int i=0;i<n;++i) {
            if(i) std::cout<<"  ";
            const auto& c=group[i];
            const int span=cell+(i<(width-2*(n-1))%n?1:0);
            string border=paint("│","90");
            if(y==-2 || y==int(rows)) std::cout<<paint((y==-2?"╭":"╰")+repeat("─",span-2)+(y==-2?"╮":"╯"),"90");
            else {
                string value=y<0?c.title:y<int(c.rows.size())?c.rows[y]:"";
                string style=y<0?"37":y==0?"1;"+c.accent:"39";
                string cellText=fit(value,span-4);
                size_t marker=cellText.find("▼");
                string amountStyle="1;31";
                if(marker==string::npos) {marker=cellText.find("▲");amountStyle="1;32";}
                std::cout<<border<<" ";
                if(y>=0 && c.title=="Your wallets" && !value.empty() && std::isdigit(static_cast<unsigned char>(value[0]))) {
                    auto keyEnd=cellText.find(' ');
                    if(keyEnd==string::npos) keyEnd=cellText.size();
                    std::cout<<paint(cellText.substr(0,keyEnd),"1;36")<<paint(cellText.substr(keyEnd),"97");
                }
                else if(y>=0 && marker!=string::npos)
                    std::cout<<paint(cellText.substr(0,marker),"39")<<paint(cellText.substr(marker),amountStyle);
                else std::cout<<paint(cellText,style);
                std::cout<<" "<<border;
            }
        }
        std::cout<<'\n';
    }
}
static void title(const string& subtitle) {
    int width=contentWidth();
    string heading=fit("Expense Tracker",std::min(15,width));
    string caption=fit(subtitle,std::min(columns(subtitle),width));
    std::cout<<'\n'<<inset(columns(heading))<<paint(heading,"1;97");
    std::cout<<'\n'<<inset(columns(caption))<<paint(caption,"37");
    std::cout<<"\n\n";
}
static string formField(const string& label,const string& hint,bool optional=false,int maximumWidth=66) {
    int w=std::min(contentWidth(),maximumWidth);
    cards({{label,{hint,""},"39"}},w);
    if(colorEnabled) {
        std::cout<<"\033[s\033[2A\r"<<inset(w)<<"\033[2C";
        string value=InputTools::prompt("",optional);
        std::cout<<"\033[u";
        return value;
    }
    std::cout<<inset(w);
    return InputTools::prompt(label,optional);
}
static int numberField(const string& label,int minimum,int maximum,int maximumWidth=76) {
    auto value=formField(label,"Enter "+std::to_string(minimum)+"–"+std::to_string(maximum),false,maximumWidth);
    if(value.size()>9 || value.find_first_not_of("0123456789")!=string::npos)
        throw std::invalid_argument("Enter a number from the options");
    int result=std::stoi(value);
    if(result<minimum || result>maximum) throw std::invalid_argument("Choose a number from the options");
    return result;
}
static Money amountField(const string& label) {
    auto amount=InputTools::parseAmount(formField(label,"Amount in rupees"));
    if(!amount.raw()) throw std::invalid_argument("Amount must exceed zero");
    return amount;
}
static string dateField(const string& label) {
    auto date=formField(label,"YYYY-MM-DD · Enter for today",true);
    if(date.empty()) date=InputTools::localStamp("%Y-%m-%d");
    if(!InputTools::validDate(date)) throw std::invalid_argument("Enter a valid calendar date");
    return date;
}
static string monthField() {
    auto month=formField("Month","YYYY-MM · Enter for this month",true);
    if(month.empty()) month=InputTools::localStamp("%Y-%m");
    if(!InputTools::validDate(month+"-01")) throw std::invalid_argument("Enter a valid month");
    return month;
}
static int actions(const string& title,const vector<string>& options) {
    vector<string> rows;
    for(size_t i=0;i<options.size();++i) rows.push_back("["+std::to_string(i+1)+"]  "+options[i]);
    rows.push_back("[0]  Back");
    cards({{title,rows,"36"}},std::min(contentWidth(),76));
    return numberField("Choose an option",0,int(options.size()));
}
static void pause() {
    if(colorEnabled) {std::cout<<margin();InputTools::prompt("Enter to return to overview",true);}
}
static string decimal(double n) {
    std::ostringstream s; s<<std::fixed<<std::setprecision(1)<<n; return s.str();
}
static string pick(const string& title,const vector<string>& choices,int perRow=3) {
    int width=std::min(contentWidth(),96);
    perRow=std::max(1,std::min(perRow,width/23));
    screen(title,9+4*((int(choices.size())+perRow-1)/perRow));
    for(size_t i=0;i<choices.size();i+=perRow) {
        vector<Card> tiles;
        for(size_t j=i;j<std::min(i+perRow,choices.size());++j) {
            auto& label=choices[j];
            tiles.push_back({"["+std::to_string(j+1)+"]",{label},label=="Expense"?"31":label=="Income"?"32":"36"});
        }
        cards(tiles,width);
    }
    return choices[numberField("Select",1,int(choices.size()),width)-1];
}
struct Column {
    string label;
    int weight=1;
    string ink="39";
    bool right=false;
};
static void table(const string& title,vector<Column> cols,const vector<vector<string>>& records) {
    const vector<Column> narrowColumns{{"Field",1,"36"},{"Value"}};
    size_t page=0;
    for(;;) {
        int width=contentWidth(),n=int(cols.size()),room=width-1-3*n;
        vector<int> widths;
        int base=0,weight=0;
        for(size_t i=0;i<cols.size();++i) {
            const auto& c=cols[i];
            int minimum=std::max(4,columns(c.label));
            for(const auto& record:records) {
                if(c.right) minimum=std::max(minimum,std::min(24,columns(record[i])));
                else {
                    std::istringstream words(record[i]);
                    for(string word;words>>word;)
                        minimum=std::max(minimum,std::min(12,columns(word)));
                }
            }
            widths.push_back(minimum);base+=minimum;weight+=c.weight;
        }
        bool compact=room<base;
        if(compact) widths={std::max(3,(width-7)/3),width-7-std::max(3,(width-7)/3)};
        else for(int i=0;i<n;++i){int extra=(room-base)*cols[i].weight/weight;widths[i]+=extra;room-=extra;weight-=cols[i].weight;}
        vector<vector<string>> lines;
        vector<size_t> recordNumbers;
        size_t recordNumber=0;
        const auto& displayed=compact?narrowColumns:cols;
        auto add=[&](const vector<string>& values) {
            vector<vector<string>> cells;size_t height=1;
            for(size_t i=0;i<values.size();++i){cells.push_back(wrap(values[i],widths[i]));height=std::max(height,cells.back().size());}
            for(size_t y=0;y<height;++y){vector<string> row;for(auto& c:cells)row.push_back(y<c.size()?c[y]:"");lines.push_back(std::move(row));recordNumbers.push_back(recordNumber);}
        };
        for(const auto& r:records) {
            if(compact){for(int i=0;i<n;++i)add({cols[i].label,r.at(i)});}
            else add(r);
            ++recordNumber;
        }
        if(lines.empty()){vector<string> empty(widths.size());empty[0]="No records";add(empty);}
        size_t capacity=std::max(1,viewport().second-12),pages=(lines.size()+capacity-1)/capacity;
        page=std::min(page,pages-1);
        size_t first=page*capacity,last=std::min(lines.size(),first+capacity);
        screen(title,int(last-first)+10);
        auto rule=[&](string left,string mid,string right){
            std::cout<<margin()<<paint(left,"90");for(size_t i=0;i<widths.size();++i)
                std::cout<<paint(repeat("─",widths[i]+2)+(i+1==widths.size()?right:mid),"90");
            std::cout<<'\n';
        };
        auto row=[&](const vector<string>& cells,bool header){
            std::cout<<margin()<<paint("│","90");
            for(size_t i=0;i<widths.size();++i){
                string ink=header?"1;36":displayed[i].ink;
                if(!header && cells[i].find("▼")!=string::npos) ink="31";
                if(!header && cells[i].find("▲")!=string::npos) ink="32";
                std::cout<<" "<<paint(fit(cells[i],widths[i],displayed[i].right),ink)<<" "<<paint("│","90");
            }
            std::cout<<'\n';
        };
        rule("╭","┬","╮");vector<string> labels;for(auto& c:displayed) labels.push_back(c.label);
        row(labels,true);rule("├","┼","┤");for(size_t i=first;i<last;++i)row(lines[i],false);
        rule("╰","┴","╯");
        string context=records.empty()?"No records to display":
            "Records "+std::to_string(recordNumbers[first]+1)+"–"+std::to_string(recordNumbers[last-1]+1)+" / "+std::to_string(records.size());
        if(first && recordNumbers[first]==recordNumbers[first-1]) context+=" · continued";
        std::cout<<margin()<<paint(fit(context,width),"37")<<'\n';
        std::cout<<margin()<<paint("Page "+std::to_string(page+1)+" / "+std::to_string(pages),"90")<<"   ";
        string navigation;
        if(page+1<pages) navigation+="[N] Next   ";
        if(page) navigation+="[P] Previous   ";
        navigation+="[Enter] Back";
        string key=InputTools::lower(InputTools::prompt(navigation,true));
        if(key.empty()||key=="0"||key=="b")return;
        if(key=="n"&&page+1<pages)++page;
        if(key=="p"&&page)--page;
    }
}
// Wrap the long narration before paginating so every character remains available.
static void browse(const string& title,const vector<string>& lines,const string& accent="36") {
    vector<vector<string>> rows;
    for(const auto& line:lines) {
        size_t split=line.find("  ");
        if(split==string::npos) split=line.find(" · ");
        if(split==string::npos) rows.push_back({line,""});
        else rows.push_back({line.substr(0,split),InputTools::trim(line.substr(split))});
    }
    table(title,{{"Item",3,accent},{"Details",5}},rows);
}
};
