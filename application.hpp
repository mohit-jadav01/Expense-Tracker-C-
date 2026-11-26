#pragma once
#include "terminal.hpp"
#include "bank.hpp"
#include "reports.hpp"
#include "calculator.hpp"
#include <cstdlib>
#include <filesystem>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
using namespace std;

// Coordinates account actions and owns their shared wallet state.
class MoneyMapApp {
private:
    Wallet w;
    BankStatementService bank;
    WalletReports reports;

// Prepares terminal encoding/colors and locks this wallet folder.
void openSession();
// Reads and validates a time; blank means now.
string readTime();
// Updates the opening amount after validation.
void openingBalances(Wallet& w,bool firstRun);
// Collects new-account settings and saves the profile.
void setup(Wallet& w,bool fresh,const set<string>& existing={});
// Finds saved profile CSV files.
vector<string> accountFiles();
// Creates or loads the selected account.
bool accounts(Wallet& current,bool activeAccount=true);
// Builds a history table, optionally filtered by month.
void records(const Wallet& w,const string& month);
// Builds a history table, optionally filtered by month.
void records(const Wallet& w);

// Edits or deletes an expense and its linked shares.
void manageExpense(Wallet& w);


// Builds the participant card for one bill.
TerminalUI::Card allocation(const Wallet& w,const Entry& bill);
// Collects an Entry, optionally stages shares, and stores the transaction.
void addManual(Wallet& w);
// Creates savings goals or allocates contributions.
void goals(Wallet& w);
// Validates people and returns equal or custom shares.
vector<Share> splitShares(const Wallet& w,const Entry& entry);
// Attaches shares to an existing paid expense.
void split(Wallet& w);
// Handles splits, repayments and pending bills.
void payments(Wallet& w);
// Updates budgets, category priorities and expense patterns.
void settings(Wallet& w);
public:
// Starts the session, routes menu choices, and saves completed changes.
// Loads the active wallet and runs the dashboard action loop.
int run();
};
