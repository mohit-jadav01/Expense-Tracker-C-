#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

using std::string;
using std::vector;
using std::map;
using std::unique_ptr;
using Cents = std::int64_t; // INR minor units (paise).

class Money {
    Cents paise;
public:
    explicit Money(Cents value = 0);
    Cents raw() const;
    Money operator+(Money other) const;
    Money operator-(Money other) const;
    bool operator>(Money other) const;
    string str() const;
};

struct Entry {
    string pattern;
    int id = 0;
    Money amount;
    string date, time, description, category, method, account, destination, due, bankKey;
};

class Transaction {
public:
    Entry entry;
    explicit Transaction(Entry data);
    virtual ~Transaction() = default;
    virtual char kind() const = 0;
    virtual Money impact() const = 0;
};

class Income : public Transaction {
public:
    using Transaction::Transaction;
    char kind() const override;
    Money impact() const override;
};
class Expense : public Transaction {
public:
    using Transaction::Transaction;
    char kind() const override;
    Money impact() const override;
};
class Transfer : public Transaction {
public:
    using Transaction::Transaction;
    char kind() const override;
    Money impact() const override;
};

struct Goal {
    string name;
    Money target, saved;
};
struct Share {
    int expenseId = 0;
    string name, note;
    Money amount;
    int receiptId = 0;
};

struct BankRow {
    string date, narration, key;
    Money amount;
    bool debit = false;
};

class Wallet {
public:
    vector<unique_ptr<Transaction>> ledger;
    map<string, Money> budgets; // YYYY-MM|category
    map<string, string> priorities;
    vector<Goal> goals;
    vector<Share> shares;
    vector<BankRow> statement;
    Money opening;
    string owner;
    string fileName = "wallet.csv";
    int add(char kind, Entry data);
    Transaction& at(int id);
    void save() const;
    void load();
};

