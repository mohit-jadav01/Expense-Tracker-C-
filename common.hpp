#pragma once
#include "wallet.hpp"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>

inline const vector<string> categories{"Food","Groceries","Shopping","Travel","Education","Entertainment","Housing","Health","Utilities","Other"};
inline const vector<string> methods{"Cash","UPI","Card","Bank"};
inline bool colorEnabled=false;
// Stateless parsing, validation, and shared input operations.
class InputTools {
public:
static string color(const string& s,int code) {
    return colorEnabled ? "\033["+std::to_string(code)+"m"+s+"\033[0m" : s;
}
static void heading(const string& title) {
    std::cout << "\n" << color(title,36) << "\n" << string(64,'-') << "\n";
}
static string column(const string& s,size_t width=22) {
    return s + string(s.size()<width ? width-s.size() : 0,' ');
}
static string trim(string s) {
    auto a=s.find_first_not_of(" \t\r\n"), b=s.find_last_not_of(" \t\r\n");
    return a==string::npos ? "" : s.substr(a,b-a+1);
}
static string lower(string s) {
    for(char& c:s) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
static string localStamp(const char* format) {
    std::time_t t=std::time(nullptr);
    std::ostringstream out;
    out << std::put_time(std::localtime(&t),format);
    return out.str();
}
static bool validDate(const string& date) {
    static const std::regex pattern(R"(\d{4}-(0[1-9]|1[0-2])-\d{2})");
    if(!std::regex_match(date,pattern)) return false;
    int y=std::stoi(date.substr(0,4)), m=std::stoi(date.substr(5,2)), d=std::stoi(date.substr(8));
    const int days[]{31,28,31,30,31,30,31,31,30,31,30,31};
    return y>=1900 && d>=1 && d<=days[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0));
}
static Money parseAmount(const string& s) {
    static const std::regex pattern(R"([0-9]{1,12}(\.[0-9]{1,2})?)");
    if(!std::regex_match(s,pattern)) throw std::invalid_argument("Use up to 12 whole digits and two decimals.");
    auto pos=s.find('.');
    auto fraction=pos==string::npos ? "00" : s.substr(pos+1)+"0";
    return Money(std::stoll(s.substr(0,pos))*100+std::stoll(fraction.substr(0,2)));
}
static string prompt(const string& label, bool optional=false) {
    std::cout << label << color(" > ",36);
    string s;
    if(!std::getline(std::cin,s)) throw std::ios_base::failure("Input closed");
    s=trim(s);
    for(unsigned char c:s) if(c<32 || c==127) throw std::invalid_argument("Use printable characters.");
    if(s.empty() && !optional) throw std::invalid_argument("A value is required.");
    return s;
}
static int number(const string& label,int low,int high) {
    string s=prompt(label);
    if(s.size()>9 || !std::all_of(s.begin(),s.end(),[](char c){return c>='0' && c<='9';}))
        throw std::invalid_argument("Enter a whole number.");
    int n=std::stoi(s);
    if(n<low || n>high) throw std::invalid_argument("Choice outside the available range.");
    return n;
}
static string choose(const string& label,const vector<string>& values) {
    for(size_t i=0;i<values.size();++i) {
        string key="["+std::to_string(i+1)+"] ";
        std::cout << color(key,36) << column(values[i],18);
        if(i%3==2 || i+1==values.size()) std::cout << '\n';
    }
    return values.at(number(label,1,int(values.size()))-1);
}
static string readDate(const string& label) {
    string d=prompt(label+" YYYY-MM-DD [today]",true);
    if(d.empty()) d=localStamp("%Y-%m-%d");
    if(!validDate(d)) throw std::invalid_argument("Invalid calendar date.");
    return d;
}
static string readMonth() {
    string m=prompt("Month YYYY-MM [current]",true);
    if(m.empty()) m=localStamp("%Y-%m");
    if(!validDate(m+"-01")) throw std::invalid_argument("Invalid month.");
    return m;
}
static Money positive(const string& label) {
    Money a=parseAmount(prompt(label+" (INR)"));
    if(!a.raw()) throw std::invalid_argument("Amount must exceed zero.");
    return a;
}
static string priority(const Wallet& w,const string& cat) {
    auto it=w.priorities.find(cat);
    if(it!=w.priorities.end()) return it->second;
    if(cat=="Housing" || cat=="Health" || cat=="Utilities") return "Must";
    if(cat=="Shopping" || cat=="Entertainment" || cat=="Other") return "Want";
    return "Need";
}


};
