#pragma once
#include "insights.hpp"
#include <fstream>
#include <set>
using namespace std;

// Imports and presents statement rows for the supplied wallet.
class BankStatementService {
private:
// A quoted narration may contain commas, doubled quotes or line breaks.
// Reads quoted CSV fields, including embedded commas and newlines.
bool csvRow(istream& in,vector<string>& fields,int& line);
// Converts a bank amount to integer paise.
Money bankAmount(string s); 
// Validates and converts a statement date.
string bankDate(const string& s);
// Extracts a recognized payment mode and counterparty.
pair<string,string> narrationDetails(const string& narration);
// Stages valid, nonduplicate BankRow records before adding them.
void importBank(Wallet& w);

struct Totals {
    Money out,in;
    int count=0;
    void add(const BankRow& r) {
        ++count;
        if(r.debit) out=out+r.amount;
        else in=in+r.amount;
    }
};
// Sorts and displays grouped bank totals.
void breakdown(const string& title,const map<string,Totals>& groups,bool byCount=false);
// Formats a month for display.
string monthLabel(const string& date);
// Formats a full date for display.
string dateLabel(const string& date);
// Builds filtered statement history, insights or full report.
void bankView(const Wallet& w,bool details,bool full=false);
public:
// Entry point used by the application dashboard.
// Runs the bank submenu and saves completed imports.
void menu(Wallet& w);
};
