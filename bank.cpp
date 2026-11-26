#include "bank.hpp"

// Reads quoted CSV fields, including embedded commas and newlines.
bool BankStatementService::csvRow(istream& in,vector<string>& fields,int& line) {
    fields.clear();
    string cell;
    bool quotedCell=false, started=false;
    char c;
    while(in.get(c)) {
        started=true;
        if(c=='\n') ++line;
        if(c=='"' && quotedCell && in.peek()=='"') {in.get(); cell+='"';}
        else if(c=='"' && (quotedCell || cell.empty())) quotedCell=!quotedCell;
        else if(c==',' && !quotedCell) {fields.push_back(InputTools::trim(cell)); cell.clear();}
        else if(c=='\n' && !quotedCell) break;
        else if(c=='\n' || c=='\r' || c=='\t') cell+=' ';
        else if(static_cast<unsigned char>(c)>=32) cell+=c;
    }
    if(quotedCell) throw invalid_argument("Unclosed quoted CSV field near line "+to_string(line));
    if(started) fields.push_back(InputTools::trim(cell));
    return started;
}

// Converts a bank amount to integer paise.
Money BankStatementService::bankAmount(string s) {
    if(s.empty()) return Money();
    if(s.find(',')!=string::npos) {
        static const regex grouped(R"(([0-9]{1,3}(,[0-9]{3})+|[0-9]{1,2}(,[0-9]{2})*,[0-9]{3})(\.[0-9]{1,2})?)");
        if(!regex_match(s,grouped)) throw invalid_argument("Invalid amount grouping.");
        s.erase(remove(s.begin(),s.end(),','),s.end());
    }
    return InputTools::parseAmount(s);
}

// Validates and converts a statement date.
string BankStatementService::bankDate(const string& s) {
    static const regex fmt(R"((\d{2})/(\d{2})/(\d{2}|\d{4}))");
    smatch parts;
    if(!regex_match(s,parts,fmt)) throw invalid_argument("Expected DD/MM/YY or DD/MM/YYYY.");
    string year=parts[3];
    if(year.size()==2) year="20"+year;
    string date=year+"-"+parts[2].str()+"-"+parts[1].str();
    if(!InputTools::validDate(date)) throw invalid_argument("Invalid calendar date.");
    return date;
}

// Extracts a recognized payment mode and counterparty.
pair<string,string> BankStatementService::narrationDetails(const string& narration) {
    string clean=InputTools::trim(narration);
    transform(clean.begin(),clean.end(),clean.begin(),[](unsigned char c){return toupper(c);});
    static const regex spaces(R"(\s+)");
    clean=regex_replace(clean,spaces," ");
    vector<string> fields;
    istringstream parts(clean);
    for(string part;getline(parts,part,'-');) fields.push_back(InputTools::trim(part));
    string prefix=fields.empty() ? "" : fields.front();
    string mode="Unidentified", party;
    if(prefix=="UPI") {
        mode="UPI";
        // The name is between UPI and the handle. A name can contain a hyphen.
        for(size_t i=2;i<fields.size();++i) {
            if(fields[i].find('@')==string::npos) continue;
            party=fields[1];
            for(size_t j=2;j<i;++j) party+="-"+fields[j];
            break;
        }
    } else if(prefix=="IMPS") {
        mode="IMPS";
        if(fields.size()>2) party=fields[2];
    } else if(prefix=="ACH C" || prefix=="ACH D" || prefix=="ACH") {
        mode="ACH";
        if(fields.size()>1) party=fields[1];
    } else if(prefix=="POS" || prefix=="CARD") {
        mode="Card";
        if(fields.size()>2) party=fields[2];
    } else if((prefix=="REV" && fields.size()>1 && fields[1]=="UPI") || prefix=="UPIRET")
        mode="UPI reversal";
    else if(prefix=="ATW" || prefix=="ATM" || prefix=="NWD") mode="ATM";
    else if(prefix=="CHQ DEP") mode="Cheque deposit";
    else if(prefix=="FT") mode="Funds transfer";
    else if(clean.rfind("INTEREST ",0)==0) mode="Bank interest";
    bool hasLetter=any_of(party.begin(),party.end(),[](unsigned char c){return isalpha(c)!=0;});
    if(!hasLetter || party.find('@')!=string::npos) party="Unidentified";
    return {mode,party};
}

// Stages valid, nonduplicate BankRow records before adding them.
void BankStatementService::importBank(Wallet& w) {
    TerminalUI::screen("Import a bank statement",15);
    string path=TerminalUI::formField("CSV file","Paste the statement file path");
    if(path.size()>1 && path.front()=='"' && path.back()=='"') path=path.substr(1,path.size()-2);
    ifstream in(path,ios::binary);
    if(!in) throw invalid_argument("Cannot open CSV file.");
    const vector<string> header{"Date","Narration","Chq./Ref.No.","Value Dt","Withdrawal Amt.","Deposit Amt.","Closing Balance"};
    vector<string> row;
    int line=1,duplicates=0,rejected=0;
    bool found=false;
    set<string> known;
    for(const auto& row:w.statement) known.insert(row.key);
    vector<BankRow> staged;
    while(true) {
        int first=line;
        if(!csvRow(in,row,line)) break;
        if(first==1 && row[0].substr(0,3)=="\xEF\xBB\xBF") row[0].erase(0,3);
        if(row==header) {found=true; continue;}
        if(!found) continue;
        if(row[0].find("STATEMENT SUMMARY")==0 || row[0]=="Opening Balance") break;
        if(row[0].find('*')==0 || all_of(row.begin(),row.end(),[](const string& s){return s.empty();})) continue;
        try {
            if(row.size()!=7) throw invalid_argument("Expected seven columns.");
            BankRow e;
            e.date=bankDate(row[0]);
            e.narration=row[1];
            Money debit=bankAmount(row[4]),credit=bankAmount(row[5]);
            if(bool(debit.raw())==bool(credit.raw())) throw invalid_argument("Expected one positive debit or credit.");
            bool spent=debit.raw()>0;
            e.amount=spent ? debit : credit;
            e.debit=spent;
            // A bank reference may repeat. Include posting details and running balance.
            ostringstream key;
            key << quoted(e.date) << quoted(row[2]) << quoted(e.narration) << (spent?'D':'C')
                << e.amount.raw() << ' ' << bankAmount(row[6]).raw();
            e.key=key.str();
            if(!known.insert(e.key).second) {++duplicates; continue;}
            staged.push_back(move(e));
        } catch(const invalid_argument& err) {
            cout << "Line " << first << " skipped: " << err.what() << '\n';
            ++rejected;
        }
    }
    if(!found) throw invalid_argument("Statement table header was not found.");
    for(auto& e:staged) w.statement.push_back(move(e));
    TerminalUI::cards({{"Import complete",{to_string(staged.size())+" added",to_string(duplicates)+" duplicates skipped",to_string(rejected)+" rejected"},"32"}},TerminalUI::contentWidth());
}

// Sorts and displays grouped bank totals.
void BankStatementService::breakdown(const string& title,const map<string,Totals>& groups,bool byCount) {
    vector<string> keys;
    for(const auto& item:groups) keys.push_back(item.first);
    stable_sort(keys.begin(),keys.end(),[&](const string& a,const string& b){
        const auto& x=groups.at(a);
        const auto& y=groups.at(b);
        if(byCount) {
            if(x.count!=y.count) return x.count>y.count;
            return a<b;
        }
        if(x.out.raw()!=y.out.raw()) return x.out>y.out;
        return x.in>y.in;
    });
    vector<vector<string>> rows;
    for(const auto& k:keys) {
        const auto& v=groups.at(k);
        rows.push_back({k,to_string(v.count),TerminalUI::money(v.out),TerminalUI::money(v.in)});
    }
    TerminalUI::table(title,{{"Name",5},{"Count",1,"97",true},{"Paid out",2,"31",true},{"Received",2,"32",true}},rows);
}

// Formats a month for display.
string BankStatementService::monthLabel(const string& date) {
    static const string names[]{"January","February","March","April","May","June","July","August","September","October","November","December"};
    return names[stoi(date.substr(5,2))-1]+" "+date.substr(0,4);
}

// Formats a full date for display.
string BankStatementService::dateLabel(const string& date) {
    return to_string(stoi(date.substr(8,2)))+" "+monthLabel(date);
}

// Builds filtered statement history, insights or full report.
void BankStatementService::bankView(const Wallet& w,bool details,bool full) {
    if(w.statement.empty()) {TerminalUI::screen("Bank activity",12); TerminalUI::cards({{"Statement library",{"No statement yet · choose Import CSV to begin"}}},TerminalUI::contentWidth());return;}
    set<string,greater<string>> available;
    for(const auto& row:w.statement) available.insert(row.date.substr(0,7));
    vector<string> months(available.begin(),available.end());
    TerminalUI::screen("Statement period",14);
    string month=TerminalUI::formField("Report period","Enter for all dates · M to select a month · or YYYY-MM",true);
    if(InputTools::lower(month)=="m") {
        vector<string> labels; for(const auto& value:months) labels.push_back(monthLabel(value));
        string selected=TerminalUI::pick("Statement month",labels);
        month=months[find(labels.begin(),labels.end(),selected)-labels.begin()];
    }
    if(!month.empty() && month.size()<=6 && month.find_first_not_of("0123456789")==string::npos) {
        int index=stoi(month);
        if(index<1 || size_t(index)>months.size()) throw invalid_argument("Choose a month from the list.");
        month=months[index-1];
    }
    if(!month.empty() && !InputTools::validDate(month+"-01")) throw invalid_argument("Invalid month.");
    map<string,Totals> modes,parties;
    Totals all;
    vector<insights::Movement> movements;
    map<string,Totals> monthly;
    map<string,vector<const BankRow*>,greater<string>> entries;
    string firstDate,latestDate,closing;
    int debits=0;
    for(const auto& r:w.statement) {
        auto [mode,party]=narrationDetails(r.narration);
        movements.push_back({r.date,"",party,r.amount,r.debit?'E':'I'});
        if(!month.empty() && r.date.substr(0,7)!=month) continue;
        if(r.date>=latestDate) {
            // The saved bank fingerprint ends with the statement's balance in paise.
            auto pos=r.key.rfind(' ');
            if(pos!=string::npos) {closing=TerminalUI::money(Money(stoll(r.key.substr(pos+1)))); latestDate=r.date;}
        }
        all.add(r);
        if(firstDate.empty() || r.date<firstDate) firstDate=r.date;
        monthly[r.date.substr(0,7)].add(r);
        entries[r.date.substr(0,7)].push_back(&r);
        modes[mode].add(r); parties[party].add(r);
        if(r.debit) ++debits;
    }
    if(!all.count) {cout<<"No transactions for this period.\n";return;}
    if(full) {
        TerminalUI::clear();TerminalUI::title(w.owner+" · Full bank statement report");
        int width=TerminalUI::contentWidth();
        auto section=[&](const string& title,const vector<string>& lines,const string& tint) {
            vector<string> wrapped;
            for(auto line:lines) {
                while(TerminalUI::columns(line)>width-4) {
                    auto end=TerminalUI::prefix(line,width-4);
                    wrapped.push_back(line.substr(0,end));line.erase(0,end);
                }
                wrapped.push_back(line);
            }
            TerminalUI::cards({{title,wrapped,tint}},width);
        };
        section("Statement summary",{dateLabel(firstDate)+" – "+dateLabel(latestDate),
            to_string(all.count)+" transactions · "+to_string(debits)+" payments · "+to_string(all.count-debits)+" receipts",
            "▲ Money received   "+TerminalUI::money(all.in),"▼ Money paid out   "+TerminalUI::money(all.out),
            "Net change   "+TerminalUI::money(all.in-all.out),
            closing.empty()?"Closing balance unavailable":"Last reported closing balance   "+closing},"36");
        for(const auto& [month,t]:monthly)
            section(monthLabel(month),{"▲ Received   "+TerminalUI::money(t.in),"▼ Paid out   "+TerminalUI::money(t.out),
                to_string(t.count)+" transactions · net "+TerminalUI::money(t.in-t.out)},"34");
        for(const auto& [mode,t]:modes)
            section("Payment method · "+mode,{to_string(t.count)+" transactions",
                "▲ Received   "+TerminalUI::money(t.in),"▼ Paid out   "+TerminalUI::money(t.out)},"35");
        vector<string> merchants;
        for(const auto& [name,t]:parties) merchants.push_back(name);
        sort(merchants.begin(),merchants.end(),[&](const string& a,const string& b){
            if(parties.at(a).count!=parties.at(b).count) return parties.at(a).count>parties.at(b).count;
            return a<b;
        });
        for(const auto& name:merchants) {
            const auto& t=parties.at(name);
            section(name,{to_string(t.count)+" transactions","▲ Received   "+TerminalUI::money(t.in),
                "▼ Paid out   "+TerminalUI::money(t.out)},"33");
        }
        for(const auto& [month,group]:entries) for(const auto* row:group) {
            auto [mode,party]=narrationDetails(row->narration);
            section(dateLabel(row->date)+" · "+party,
                {(row->debit?"▼ Paid out   ":"▲ Received   ")+TerminalUI::money(row->amount),
                 "Payment method   "+mode,"Narration   "+row->narration},row->debit?"31":"32");
        }
        cout<<TerminalUI::margin();InputTools::prompt("End of bank report · Enter to return",true);
        return;
    }
    if(details) {
        vector<vector<string>> rows;
        for(auto& [key,group]:entries) {
            stable_sort(group.begin(),group.end(),[](auto a,auto b){return a->date>b->date;});
            for(auto r:group) {
                auto [mode,party]=narrationDetails(r->narration);
                rows.push_back({dateLabel(r->date),mode,party,(r->debit?"▼ ":"▲ ")+TerminalUI::money(r->amount),r->narration});
            }
        }
        TerminalUI::table("Statement transactions",{{"Date",2},{"Method",2},{"Counterparty",3},{"Amount",2,"97",true},{"Narration",6}},rows); return;
    }
    for(;;) {
        TerminalUI::clear();
        TerminalUI::title("Bank statement   /   "+monthLabel(firstDate)+(firstDate.substr(0,7)!=latestDate.substr(0,7)?" - "+monthLabel(latestDate):""));
        TerminalUI::cards({{"Money received",{TerminalUI::money(all.in),to_string(all.count-debits)+" deposits"},"32"},
                   {"Money paid out",{TerminalUI::money(all.out),to_string(debits)+" withdrawals"},"31"},
                   {"Net change",{TerminalUI::money(all.in-all.out)},"36"}},TerminalUI::contentWidth());
        TerminalUI::cards({{"Closing balance",{closing,dateLabel(latestDate)},"36"}},TerminalUI::contentWidth());
        TerminalUI::cards({{"Explore this statement",{"1  Monthly activity","2  Payment methods","3  Merchants and people","4  Trends and comparisons","0  Back"},"35"}},TerminalUI::contentWidth());
        int view=TerminalUI::numberField("Choose an option",0,4);
        if(!view) return;
        if(view==1) {
            vector<vector<string>> rows;
            for(auto it=monthly.rbegin();it!=monthly.rend();++it) {
                auto& t=it->second;
                rows.push_back({monthLabel(it->first),TerminalUI::money(t.in),TerminalUI::money(t.out),TerminalUI::money(t.in-t.out)});
            }
            TerminalUI::table("Monthly activity",{{"Month",3},{"Received",2,"32",true},{"Paid out",2,"31",true},{"Net",2,"97",true}},rows);
        }
        if(view==2) breakdown("Payment methods",modes);
        if(view==3) breakdown("Merchants and people by transaction count",parties,true);
        if(view==4) insights::analyze(movements,month,true);
    }
}

// Runs the bank submenu and saves completed imports.
void BankStatementService::menu(Wallet& w) {
    for(;;) {
        TerminalUI::clear();TerminalUI::title("Bank statements");
        TerminalUI::cards({{"Statement library",{to_string(w.statement.size())+" saved transactions"},"36"},
                   {"Actions",{"1  Import CSV","2  Browse transactions","3  Statement insights","4  Full report","0  Back"},"35"}},TerminalUI::contentWidth());
        int n=TerminalUI::numberField("Choose an option",0,4);
        if(!n) return;
        if(n==1) {importBank(w); w.save();TerminalUI::pause();}
        else bankView(w,n==2,n==4);
    }
}
