#pragma once
#include "insights.hpp"
using namespace std;

// Read-only views of the active wallet.
class WalletReports {
public:

// Displays all wallet details in a continuous report.
void fullReport(const Wallet& w);

private:
// Calculates a percentage with a zero-denominator guard.
static double percent(Money value,Money base);
struct Summary {
    Money income,expense,net,balance;
    int incomes=0,expenses=0,transfers=0;
    map<string,Money> category,day,byPriority;
    map<string,int> categoryCount,methodCount;
    const Entry *largest=nullptr,*smallest=nullptr;
};
// Accumulates wallet totals using virtual transaction behavior.
Summary summarize(const Wallet& w,const string& month);
// Finds unusual expenses relative to previous spending.
vector<string> flags(const Wallet& w,const string& month);
public:
// Displays the current wallet overview and shortcuts.
void dashboard(const Wallet& w);
// Displays budget usage for the selected month.
static void budgetReport(const Wallet& w,const string& month,const Summary& s);
// Runs the wallet statistics views.
void report(const Wallet& w,const string& m);
};
