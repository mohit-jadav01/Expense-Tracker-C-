#include "wallet.hpp"
#include <filesystem>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace std;

Money::Money(Cents value) : paise(value) {}
Cents Money::raw() const { return paise; }
Money Money::operator+(Money other) const {
    auto n = other.paise;
    if ((n > 0 && paise > numeric_limits<Cents>::max() - n) ||
        (n < 0 && paise < numeric_limits<Cents>::min() - n))
        throw overflow_error("Total is too large.");
    return Money(paise + n);
}
Money Money::operator-(Money other) const {
    if (other.paise == numeric_limits<Cents>::min()) throw overflow_error("Amount is too large.");
    return *this + Money(-other.paise);
}
bool Money::operator>(Money other) const { return paise > other.paise; }
string Money::str() const {
    ostringstream out;
    out << (paise < 0 ? "-" : "") << abs(paise / 100) << '.' << setw(2)
        << setfill('0') << abs(paise % 100);
    return out.str();
}

Transaction::Transaction(Entry data) : entry(move(data)) {}
char Income::kind() const { return 'I'; }
char Expense::kind() const { return 'E'; }
char Transfer::kind() const { return 'T'; }
Money Income::impact() const { return entry.amount; }
Money Expense::impact() const { return entry.due.empty() ? Money() - entry.amount : Money(); }
Money Transfer::impact() const { return Money(); }

int Wallet::add(char kind, Entry data) {
    if (data.amount.raw() <= 0) throw invalid_argument("Enter a positive amount.");
    if (!data.id) data.id = ledger.empty() ? 1 : ledger.back()->entry.id + 1;
    int id = data.id;
    if (kind == 'I') ledger.push_back(make_unique<Income>(move(data)));
    else if (kind == 'E') ledger.push_back(make_unique<Expense>(move(data)));
    else if (kind == 'T') ledger.push_back(make_unique<Transfer>(move(data)));
    else throw invalid_argument("Unknown transaction type.");
    return id;
}
Transaction& Wallet::at(int id) {
    for (auto& tx : ledger) if (tx->entry.id == id) return *tx;
    throw invalid_argument("Unknown record ID.");
}

namespace {
enum Field { Kind, Id, Date, Time, Name, Description, Amount, Target, Category, Method,
             Account, Destination, Due, Reference, Link, Status, FieldCount };
using Row = array<string,FieldCount>;
const Row header{"record","id","date","time","name","description","amount_paise","target_paise",
                 "category","method","account","destination","due","reference","linked_id","status"};

void writeRow(ostream& out,const Row& row) {
    for(size_t i=0;i<row.size();++i) {
        if(i) out << ',';
        out << '"';
        for(char c:row[i]) {if(c=='"') out << '"'; out << c;}
        out << '"';
    }
    out << '\n';
}
Row readRow(const string& line) {
    Row row{};
    bool quotedField=false;
    size_t col=0;
    for(size_t i=0;i<line.size();++i) {
        char c=line[i];
        if(c=='"') {
            if(quotedField && i+1<line.size() && line[i+1]=='"') {row[col]+='"'; ++i;}
            else quotedField=!quotedField;
        } else if(c==',' && !quotedField) {
            if(++col>=FieldCount) throw runtime_error("Extra wallet.csv column.");
        } else row[col]+=c;
    }
    if(quotedField || col+1!=FieldCount) throw runtime_error("Incomplete wallet.csv row.");
    return row;
}
void loadCsv(Wallet& w) {
    ifstream in(w.fileName);
    string line;
    if(!getline(in,line) || readRow(line)!=header) throw runtime_error("Invalid wallet.csv header.");
    while(getline(in,line)) {
        Row r=readRow(line);
        const auto& type=r[Kind];
        if(type=="end" && !w.owner.empty()) return;
        if(type=="profile") {w.owner=r[Name]; w.opening=Money(stoll(r[Amount]));}
        else if(type=="budget") w.budgets[r[Date]+"|"+r[Category]]=Money(stoll(r[Amount]));
        else if(type=="priority") w.priorities[r[Category]]=r[Status];
        else if(type=="goal") w.goals.push_back({r[Name],Money(stoll(r[Target])),Money(stoll(r[Amount]))});
        else if(type=="share") w.shares.push_back({stoi(r[Id]),r[Name],r[Description],Money(stoll(r[Amount])),stoi(r[Link])});
        else if(type=="statement") w.statement.push_back({r[Date],r[Description],r[Reference],Money(stoll(r[Amount])),r[Status]=="debit"});
        else if(type=="income" || type=="expense" || type=="transfer") {
            Entry e;
            e.id=stoi(r[Id]); e.date=r[Date]; e.time=r[Time]; e.amount=Money(stoll(r[Amount]));
            e.description=r[Description]; e.category=r[Category]; e.method=r[Method]; e.account=r[Account];
            e.destination=r[Destination]; e.due=r[Due]; e.bankKey=r[Reference]; e.pattern=r[Status];
            w.add(type=="expense"?'E':type=="income"?'I':'T',move(e));
        } else throw runtime_error("Unknown wallet.csv record.");
    }
    throw runtime_error("Incomplete wallet.csv.");
}
}
void Wallet::save() const {
    filesystem::path target(fileName);
    filesystem::path temp=target;
    temp += ".tmp";
    ofstream out(temp,ios::trunc);
    out.exceptions(ios::badbit|ios::failbit);
    writeRow(out,header);
    Row profile{};
    profile[Kind]="profile"; profile[Name]=owner; profile[Amount]=to_string(opening.raw());
    writeRow(out,profile);
    for(const auto& tx:ledger) {
        const auto& e=tx->entry;
        Row r{};
        r[Kind]=tx->kind()=='E'?"expense":tx->kind()=='I'?"income":"transfer";
        r[Id]=to_string(e.id); r[Date]=e.date; r[Time]=e.time; r[Description]=e.description;
        r[Amount]=to_string(e.amount.raw()); r[Category]=e.category; r[Method]=e.method;
        r[Account]=e.account; r[Destination]=e.destination; r[Due]=e.due; r[Reference]=e.bankKey; r[Status]=e.pattern;
        writeRow(out,r);
    }
    for(const auto& [key,limit]:budgets) {
        Row r{}; r[Kind]="budget"; r[Date]=key.substr(0,7); r[Category]=key.substr(8);
        r[Amount]=to_string(limit.raw()); writeRow(out,r);
    }
    for(const auto& [cat,priority]:priorities) {
        Row r{}; r[Kind]="priority"; r[Category]=cat; r[Status]=priority; writeRow(out,r);
    }
    for(const auto& g:goals) {
        Row r{}; r[Kind]="goal"; r[Name]=g.name; r[Amount]=to_string(g.saved.raw());
        r[Target]=to_string(g.target.raw()); writeRow(out,r);
    }
    for(const auto& s:shares) {
        Row r{}; r[Kind]="share"; r[Id]=to_string(s.expenseId); r[Link]=to_string(s.receiptId);
        r[Name]=s.name; r[Description]=s.note; r[Amount]=to_string(s.amount.raw()); writeRow(out,r);
    }
    for(const auto& b:statement) {
        Row r{}; r[Kind]="statement"; r[Date]=b.date; r[Description]=b.narration; r[Reference]=b.key;
        r[Status]=b.debit?"debit":"credit"; r[Amount]=to_string(b.amount.raw()); writeRow(out,r);
    }
    Row end{}; end[Kind]="end"; writeRow(out,end);
    out.close();
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw runtime_error("Could not replace wallet.csv.");
#else
    filesystem::rename(temp,target);
#endif
}
void Wallet::load() {
    if(filesystem::exists(fileName)) {loadCsv(*this); return;}
    if(fileName!="wallet.csv") throw runtime_error("Profile file is missing.");
    if (!filesystem::exists("wallet.dat")) return;
    ifstream in("wallet.dat");
    string line;
    if (!getline(in,line) || (line != "MoneyMap-1" && line != "MoneyMap-2" && line != "MoneyMap-3" && line != "MoneyMap-4"))
        throw runtime_error("Unrecognized wallet.dat.");
    bool legacy = line == "MoneyMap-1";
    bool singleBalance = line == "MoneyMap-4";
    while (getline(in,line) && line != "END") {
        istringstream row(line);
        row.exceptions(ios::badbit | ios::failbit);
        char type;
        Cents a,b;
        string k,v;
        row >> type;
        if (type == 'B') { row >> quoted(k) >> a; budgets[k] = Money(a); }
        else if (type == 'O') {
            if (!singleBalance) row >> quoted(k);
            row >> a;
            opening = opening + Money(a);
        }
        else if (type == 'P') { row >> quoted(k) >> quoted(v); priorities[k] = v; }
        else if (type == 'G') { row >> quoted(k) >> a >> b; goals.push_back({k,Money(a),Money(b)}); }
        else if (type == 'S') {
            Share s;
            row >> s.expenseId >> quoted(s.name) >> quoted(s.note) >> a >> s.receiptId;
            s.amount = Money(a);
            shares.push_back(s);
        } else if (type == 'A') {
            BankRow b;
            row >> quoted(b.date) >> quoted(b.narration) >> quoted(b.key) >> a >> b.debit;
            b.amount = Money(a);
            statement.push_back(move(b));
        } else {
            Entry e;
            if (!legacy) row >> e.id;
            row >> a >> quoted(e.date) >> quoted(e.time) >> quoted(e.description) >> quoted(e.category)
                >> quoted(e.method) >> quoted(e.account) >> quoted(e.destination) >> quoted(e.due) >> quoted(e.bankKey);
            e.amount = Money(a);
            add(type,move(e));
        }
    }
    if (line != "END") throw runtime_error("Incomplete wallet.dat.");
    if (legacy) {
        // Keep record IDs stable, especially for bills with existing settlements.
        set<int> linkedBills;
        for (const auto& share : shares) linkedBills.insert(share.expenseId);
        ledger.erase(remove_if(ledger.begin(),ledger.end(),[&](const auto& tx) {
            auto& e = tx->entry;
            if (e.bankKey.empty()) return false;
            statement.push_back({e.date,e.description,e.bankKey,e.amount,tx->kind()=='E'});
            e.bankKey.clear();
            return !linkedBills.count(e.id);
        }),ledger.end());
    }
}

