#include "application.hpp"

// Prepares terminal encoding/colors and locks this wallet folder.
void MoneyMapApp::openSession() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    static HANDLE lock=CreateFileW(L"wallet.lock",GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(lock==INVALID_HANDLE_VALUE) throw runtime_error("Wallet folder is busy or read-only.");
    HANDLE console=GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    colorEnabled=GetConsoleMode(console,&mode) && SetConsoleMode(console,mode|ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
    static int lock=open("wallet.lock",O_CREAT|O_RDWR,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB)<0) throw runtime_error("Wallet folder is busy or read-only.");
    colorEnabled=isatty(STDOUT_FILENO);
#endif
    if(getenv("NO_COLOR")) colorEnabled=false;
}

// Reads and validates a time; blank means now.
string MoneyMapApp::readTime() {
    string t=TerminalUI::formField("Time","HH:MM · Enter for now",true);
    if(t.empty()) return InputTools::localStamp("%H:%M");
    static const regex clock(R"(([01][0-9]|2[0-3]):[0-5][0-9])");
    if(!regex_match(t,clock)) throw invalid_argument("Invalid time.");
    return t;
}

// Updates the opening amount after validation.
void MoneyMapApp::openingBalances(Wallet& w,bool firstRun) {
    if(!firstRun) TerminalUI::screen("Opening balance",14);
    for(;;) {
        try {
            string value=TerminalUI::formField("Opening balance","INR before your first entry ["+w.opening.str()+"]",true);
            if(!value.empty()) w.opening=InputTools::InputTools::parseAmount(value);
            break;
        } catch(const invalid_argument& err) {TerminalUI::cards({{"Check your entry",{err.what()},"33"}},min(TerminalUI::contentWidth(),76));}
    }
}

// Collects new-account settings and saves the profile.
void MoneyMapApp::setup(Wallet& w,bool fresh,const set<string>& existing) {
    TerminalUI::clear();
    TerminalUI::title("Set up your wallet");
    w.owner=TerminalUI::formField("Your name","Enter the name for your wallet");
    if(existing.count(InputTools::lower(w.owner))) throw invalid_argument("This account already exists. Choose it in Accounts.");
    if(fresh) {
        openingBalances(w,true);
        string limit=TerminalUI::formField("Monthly budget","Spending limit in INR (Enter to skip)",true);
        if(!limit.empty()) {
            auto cap=InputTools::InputTools::parseAmount(limit);
            if(!cap.raw()) throw invalid_argument("Budget must exceed zero.");
            w.budgets[InputTools::localStamp("%Y-%m")+"|*"]=cap;
        }
        string choice=TerminalUI::formField("Savings goal","1  Skip      2  Create a goal",true);
        if(!choice.empty() && choice!="1" && choice!="2") throw invalid_argument("Choose 1 or 2.");
        if(choice=="2") {
            string name=TerminalUI::formField("Goal name","What are you saving for?");
            auto amount=InputTools::InputTools::parseAmount(TerminalUI::formField("Target amount","Enter your savings target in INR"));
            if(!amount.raw()) throw invalid_argument("Target must exceed zero.");
            w.goals.push_back({name,amount,Money()});
        }
    }
    w.save();
}

// Finds saved profile CSV files.
vector<string> MoneyMapApp::accountFiles() {
    vector<string> files{"wallet.csv"};
    static const regex profile(R"(wallet-profile-[1-9][0-9]*\.csv)");
    for(const auto& entry:filesystem::directory_iterator(".")) {
        string name=entry.path().filename().string();
        if(entry.is_regular_file() && regex_match(name,profile)) files.push_back(name);
    }
    sort(files.begin()+1,files.end());
    return files;
}

// Creates or loads the selected account.
bool MoneyMapApp::accounts(Wallet& current,bool activeAccount) {
    auto files=accountFiles();
    vector<string> options;
    set<string> names;
    for(const auto& file:files) {
        Wallet wallet;
        wallet.fileName=file;
        wallet.load();
        names.insert(InputTools::lower(wallet.owner));
        string label=wallet.owner+(activeAccount && file==current.fileName?" (current)":"");
        options.push_back(to_string(options.size()+1)+"  "+label);
    }
    int create=int(files.size())+1;
    int width=min(TerminalUI::contentWidth(),58);
    TerminalUI::clear();
    cout<<string(max(0,(TerminalUI::viewport().second-int(options.size())-19)/2),'\n');
    const string heading="MoneyMap";
    cout<<TerminalUI::inset(TerminalUI::columns(heading))<<TerminalUI::paint(heading,"1;97")<<"\n";
    const string tagline=TerminalUI::fit("Expense Tracker",min(15,width));
    cout<<TerminalUI::inset(TerminalUI::columns(tagline))<<TerminalUI::paint(tagline,"36")<<"\n";
    cout<<TerminalUI::inset(min(24,width))<<TerminalUI::paint(TerminalUI::repeat("━",min(24,width)),"33")<<"\n\n";
    string subtitle=TerminalUI::fit("Select your account",min(19,width));
    cout<<TerminalUI::inset(TerminalUI::columns(subtitle))<<TerminalUI::paint(subtitle,"37")<<"\n\n";
    vector<string> choices;
    for(const auto& option:options) {choices.push_back(option);choices.push_back("");}
    choices.push_back(to_string(create)+"  Create new account");
    choices.push_back("0  "+string(activeAccount?"Back":"Quit"));
    TerminalUI::cards({{"Your wallets",choices,"36"}},width);
    cout<<'\n';
    int choice=TerminalUI::numberField("Account",0,create,width);
    if(!choice) return false;
    Wallet next;
    if(choice==create) {
        int id=1;
        do {next.fileName="wallet-profile-"+to_string(id++)+".csv";}
        while(filesystem::exists(next.fileName));
        setup(next,true,names);
    } else {
        if(activeAccount && files[choice-1]==current.fileName) return true;
        next.fileName=files[choice-1];
        next.load();
    }
    current=move(next);
    return true;
}

// Builds a history table, optionally filtered by month.
void MoneyMapApp::records(const Wallet& w,const string& month) {
    vector<vector<string>> rows;
    for(auto it=w.ledger.rbegin();it!=w.ledger.rend();++it) {
        const auto& t=**it;const auto& e=t.entry;
        if(!month.empty() && e.date.substr(0,7)!=month) continue;
        string type=t.kind()=='I'?"Income":t.kind()=='E'?"Expense":"Transfer";
        string description=e.description;
        if(!e.due.empty())description+=" · Due "+e.due;
        if(!e.destination.empty())description+=" · "+e.account+" → "+e.destination;
        string sign=t.kind()=='I'?"▲ ":t.kind()=='E'?"▼ ":"↔ ";
        rows.push_back({to_string(e.id),TerminalUI::shortDate(e.date)+" "+e.date.substr(0,4)+" "+e.time,type,
            sign+TerminalUI::money(e.amount),e.category,description,e.method});
    }
    TerminalUI::table("Wallet history · "+(month.empty()?string("All dates"):month),
        {{"ID"},{"Date / time",2},{"Type"},{"Amount",2,"97",true},{"Category",2},{"Description",4},{"Method"}},rows);
}

// Builds a history table, optionally filtered by month.
void MoneyMapApp::records(const Wallet& w) {records(w,"");}

// Edits or deletes an expense and its linked shares.
void MoneyMapApp::manageExpense(Wallet& w) {
    vector<string> expenses;
    for(auto it=w.ledger.rbegin();it!=w.ledger.rend();++it) {
        const auto& e=(*it)->entry;
        if((*it)->kind()=='E') expenses.push_back("#"+to_string(e.id)+"   "+TerminalUI::money(e.amount)+"   "+TerminalUI::shortDate(e.date)+" "+e.date.substr(0,4)+"   "+e.category+"   "+e.description);
    }
    TerminalUI::screen("Manage wallet expenses",17);
    if(expenses.empty()) {TerminalUI::cards({{"Expenses",{"No expenses yet · use [N] Add to record one"}}},TerminalUI::contentWidth());return;}
    vector<string> recent(expenses.begin(),expenses.begin()+min<size_t>(3,expenses.size()));
    TerminalUI::cards({{"Recent expenses",recent}},TerminalUI::contentWidth());
    string action=InputTools::lower(TerminalUI::formField("Choose an expense","Enter an ID · L for all expenses · Enter to cancel",true));
    if(action=="l") {TerminalUI::browse("All expenses",expenses);action=TerminalUI::formField("Expense ID","Enter to cancel",true);}
    if(action.empty()) return;
    if(action.size()>9 || action.find_first_not_of("0123456789")!=string::npos) throw invalid_argument("Enter the numeric expense ID.");
    int id=stoi(action);
    auto& t=w.at(id);
    if(t.kind()!='E') throw invalid_argument("Choose a wallet expense.");
    auto& e=t.entry;
    TerminalUI::screen("Expense #"+to_string(id),18);
    TerminalUI::cards({{"Expense #"+to_string(id),{e.category+"   INR "+e.amount.str(),e.description,e.date+"   "+e.time+"   "+e.method}}},TerminalUI::contentWidth());
    int mode=TerminalUI::actions("Expense actions",{"Edit expense","Delete expense"});
    if(!mode) return;
    set<int> receipts;
    Money owedShares;
    int shares=0;
    for(const auto& s:w.shares) if(s.expenseId==id) {
        ++shares;owedShares=owedShares+s.amount;
        if(s.receiptId) receipts.insert(s.receiptId);
    }
    if(mode==2) {
        TerminalUI::screen("Delete expense #"+to_string(id),16);
        TerminalUI::cards({{"Delete expense #"+to_string(id),{"Remove INR "+e.amount.str()+" from expense history.",
                   "Also removes "+to_string(shares)+" split shares and "+to_string(receipts.size())+" linked repayment records."},"31"}},TerminalUI::contentWidth());
        if(TerminalUI::formField("Confirm deletion","Type DELETE · Enter to cancel",true)!="DELETE") return;
        w.shares.erase(remove_if(w.shares.begin(),w.shares.end(),[&](const Share& s){return s.expenseId==id;}),w.shares.end());
        w.ledger.erase(remove_if(w.ledger.begin(),w.ledger.end(),[&](const auto& tx){return tx->entry.id==id || receipts.count(tx->entry.id);}),w.ledger.end());
        TerminalUI::cards({{"Expense deleted",{"The balance and linked bill shares have been updated"},"32"}},TerminalUI::contentWidth());return;
    }
    TerminalUI::screen("Edit expense #"+to_string(id),18);
    int field=TerminalUI::actions("Edit a field",{"Amount","Category","Description","Date and time","Payment method"});
    if(!field) return;
    Entry draft=e;
    if(field==1) {
        draft.amount=TerminalUI::amountField("New amount");
        if(owedShares>draft.amount) throw invalid_argument("Amount must cover the existing split shares of INR "+owedShares.str()+".");
    } else if(field==2) draft.category=TerminalUI::pick("Choose a category",categories);
    else if(field==3) draft.description=TerminalUI::formField("Description","Enter to clear",true);
    else if(field==4) {
        draft.date=TerminalUI::dateField("New date");draft.time=readTime();
        for(int receipt:receipts) if(draft.date>w.at(receipt).entry.date) throw invalid_argument("Expense date must be on or before its repayments.");
    } else draft.method=TerminalUI::pick("Payment method",methods);
    e=move(draft);TerminalUI::cards({{"Expense updated",{"#"+to_string(id)+" · "+TerminalUI::money(e.amount)},"32"}},TerminalUI::contentWidth());
}

// Builds the participant card for one bill.
TerminalUI::Card MoneyMapApp::allocation(const Wallet& w,const Entry& bill) {
    TerminalUI::Card card{"Bill #"+to_string(bill.id)+" · participants",{},"36"};
    Money mine=bill.amount;
    for(size_t i=0;i<w.shares.size();++i) {
        const auto& s=w.shares[i];
        if(s.expenseId!=bill.id) continue;
        mine=mine-s.amount;
        card.rows.push_back("["+to_string(i+1)+"] "+s.name+"   "+TerminalUI::money(s.amount)+(s.receiptId?" · received":" · owes you"));
    }
    card.rows.insert(card.rows.begin(),"Me ("+w.owner+")   "+TerminalUI::money(mine)+" · my share");
    card.title+=" ("+to_string(card.rows.size())+")";
    return card;
}

// Collects an Entry, optionally stages shares, and stores the transaction.
void MoneyMapApp::addManual(Wallet& w) {
    string type=TerminalUI::pick("Add a transaction",{"Expense","Income","Transfer"});
    string title="Add "+InputTools::lower(type);
    Entry e;
    try {e.amount=calculator::input(title);}
    catch(const calculator::Cancelled&) {return;}
    string summary=title+" · "+TerminalUI::money(e.amount);
    if(type=="Expense") e.category=TerminalUI::pick(summary+" · Category",categories);
    else if(type=="Income") e.category=TerminalUI::pick(summary+" · Category",{"Salary","Allowance","Gift","Interest","Other income"});
    else e.category="Transfer";
    e.method=TerminalUI::pick(summary+" · Payment method",methods);
    TerminalUI::screen(title+" · Details",23);
    TerminalUI::cards({{"Amount",{TerminalUI::money(e.amount)},type=="Expense"?"31":type=="Income"?"32":"36"},
               {"Category",{e.category}},{"Payment method",{e.method}}},TerminalUI::contentWidth());
    e.description=TerminalUI::formField("Description","Optional · press Enter to skip",true);
    for(;;) {
        e.date=TerminalUI::formField("Date","YYYY-MM-DD · Enter for today",true);
        if(e.date.empty()) e.date=InputTools::localStamp("%Y-%m-%d");
        if(InputTools::validDate(e.date)) break;
        cout<<TerminalUI::margin()<<"Enter a valid calendar date.\n";
    }
    for(;;) {
        e.time=TerminalUI::formField("Time","HH:MM · Enter for now",true);
        if(e.time.empty()) e.time=InputTools::localStamp("%H:%M");
        static const regex validTime(R"(([01][0-9]|2[0-3]):[0-5][0-9])");
        if(regex_match(e.time,validTime))break;
        cout<<TerminalUI::margin()<<"Use a 24-hour time, for example 14:30.\n";
    }
    vector<Share> shares;
    if(type=="Expense" && TerminalUI::pick(summary+" · Bill sharing",{"Just me","Split bill"})=="Split bill") shares=splitShares(w,e);
    int id=w.add(type[0],move(e));
    for(auto& share:shares) {share.expenseId=id; w.shares.push_back(move(share));}
    TerminalUI::screen("Transaction saved",11);
    const auto& saved=w.at(id).entry;
    TerminalUI::cards({{type+" #"+to_string(id),{summary,saved.category+" · "+saved.method,TerminalUI::shortDate(saved.date)+" · "+saved.time},"32"}},std::min(66,TerminalUI::contentWidth()));
    if(!shares.empty()) TerminalUI::cards({allocation(w,saved)},min(66,TerminalUI::contentWidth()));
}

// Creates savings goals or allocates contributions.
void MoneyMapApp::goals(Wallet& w) {
    TerminalUI::screen("Savings goals",12+4*int(w.goals.size()));
    if(w.goals.empty()) TerminalUI::cards({{"Your goals",{"No goals yet · choose New goal to set a target"}}},min(TerminalUI::contentWidth(),76));
    for(size_t i=0;i<w.goals.size();++i) {
        auto& g=w.goals[i];
        double ratio=double(g.saved.raw())/g.target.raw();
        TerminalUI::cards({{"["+to_string(i+1)+"] "+g.name,{TerminalUI::meter(ratio,14)+"  "+TerminalUI::decimal(100*ratio)+"%",TerminalUI::money(g.saved)+" of "+TerminalUI::money(g.target)+"   ·   "+TerminalUI::money(g.target-g.saved)+" left"},"32"}},min(TerminalUI::contentWidth(),76));
    }
    int mode=TerminalUI::actions("Goal actions",{"New goal","Contribute"});
    if(mode==1) {
        TerminalUI::screen("New savings goal",18);
        string name=TerminalUI::formField("Goal name","What are you saving for?");
        auto target=TerminalUI::amountField("Target");
        w.goals.push_back({name,target,Money()});
    } else if(mode==2) {
        if(w.goals.empty()) throw invalid_argument("Create a goal first.");
        TerminalUI::screen("Contribute to a goal",20);
        auto& g=w.goals[TerminalUI::numberField("Goal number",1,int(w.goals.size()))-1];
        Money a=TerminalUI::amountField("Contribution");
        if(a>g.target-g.saved) throw invalid_argument("Contribution exceeds the remaining target.");
        Money balance=w.opening,reserved;
        for(const auto& t:w.ledger) balance=balance+t->impact();
        for(const auto& goal:w.goals) reserved=reserved+goal.saved;
        if(a>balance-reserved) throw invalid_argument("Insufficient unallocated ledger balance.");
        g.saved=g.saved+a;
        double ratio=double(g.saved.raw())/g.target.raw();
        TerminalUI::screen("Contribution saved",14);
        TerminalUI::cards({{"Contribution saved · "+g.name,{TerminalUI::money(a)+" added",
            TerminalUI::meter(ratio,20)+" "+TerminalUI::decimal(100*ratio)+"%",
            TerminalUI::money(g.saved)+" of "+TerminalUI::money(g.target)},"32"}},min(TerminalUI::contentWidth(),76));
    }
}

// Validates people and returns equal or custom shares.
vector<Share> MoneyMapApp::splitShares(const Wallet& w,const Entry& entry) {
    TerminalUI::screen("Split expense · "+TerminalUI::money(entry.amount),16);
    string note=TerminalUI::formField("Split description","Enter a note or keep the expense description",true);
    if(note.empty()) note=entry.description;
    int people=TerminalUI::numberField("Total people, including me (2–51)",2,51);
    bool equal=TerminalUI::pick("Split method",{"Equal","Custom amounts"})=="Equal";
    Cents total=entry.amount.raw();
    if(equal && total<people) throw invalid_argument("Amount is too small for that many shares.");
    Money left=entry.amount;
    auto nameKey=[](const string& value) {
        static const regex spaces(R"(\s+)");
        return InputTools::lower(regex_replace(InputTools::trim(value),spaces," "));
    };
    set<string> names{nameKey(w.owner),"me"};
    vector<Share> staged;
    for(int i=1;i<people;++i) {
        string name,hint="Enter the next person's name";
        for(;;) {
            TerminalUI::screen("Split expense · "+TerminalUI::money(entry.amount),16);
            TerminalUI::Card roster{"Already included",{"Me ("+w.owner+")"},"36"};
            for(const auto& s:staged) roster.rows.push_back(s.name);
            TerminalUI::cards({roster},min(66,TerminalUI::contentWidth()));
            name=TerminalUI::formField("Person "+to_string(i+1)+" of "+to_string(people),hint,true);
            if(!name.empty() && names.insert(nameKey(name)).second) break;
            hint=name.empty()?"Enter a name to continue":"Already included · enter a different name";
        }
        // Participant 0 is the payer and gets the first spare paise.
        Money a=equal?Money(total/people+(i<total%people)):InputTools::parseAmount(TerminalUI::formField("Share for "+name,"Enter the amount in rupees"));
        if(!a.raw()) throw invalid_argument("Each share must be positive.");
        if(a>left) throw invalid_argument("Shares exceed the bill.");
        left=left-a;
        staged.push_back({entry.id,name,note,a,0});
    }
    return staged;
}

// Attaches shares to an existing paid expense.
void MoneyMapApp::split(Wallet& w) {
    vector<vector<string>> rows;
    for(const auto& t:w.ledger) if(t->kind()=='E' && t->entry.due.empty())
        rows.push_back({to_string(t->entry.id),TerminalUI::shortDate(t->entry.date),t->entry.description,TerminalUI::money(t->entry.amount)});
    if(rows.empty()) {TerminalUI::cards({{"Split a bill",{"Add an expense before splitting a bill"}}},TerminalUI::contentWidth());return;}
    TerminalUI::table("Choose a bill · note its ID",{{"ID"},{"Date",2},{"Description",4},{"Paid",2,"31",true}},rows);
    int id=TerminalUI::numberField("Paid expense ID",1,999999999);
    auto& tx=w.at(id);
    if(tx.kind()!='E' || !tx.entry.due.empty()) throw invalid_argument("Select a paid expense.");
    for(auto& s:w.shares) if(s.expenseId==id) throw invalid_argument("This expense already has a split.");
    auto staged=splitShares(w,tx.entry);
    w.shares.insert(w.shares.end(),staged.begin(),staged.end());
    TerminalUI::cards({allocation(w,tx.entry)},min(66,TerminalUI::contentWidth()));
}

// Handles splits, repayments and pending bills.
void MoneyMapApp::payments(Wallet& w) {
    TerminalUI::screen("Split bills & repayments",16);
    Money outgoing,incoming;
    TerminalUI::Card due{"Pending bills",{},"33"},shares{"Split payments",{},"36"};
    for(const auto& t:w.ledger) if(!t->entry.due.empty()) {
        const auto& e=t->entry;outgoing=outgoing+e.amount;
        due.rows.push_back("#"+to_string(e.id)+"   "+e.description+"   "+TerminalUI::money(e.amount)+" · due "+e.due);
    }
    set<int> shown;
    for(size_t i=0;i<w.shares.size();++i) {
        const auto& s=w.shares[i];if(!s.receiptId)incoming=incoming+s.amount;
        if(shown.insert(s.expenseId).second) {
            auto bill=allocation(w,w.at(s.expenseId).entry);
            shares.rows.push_back(bill.title+" · "+s.note);
            shares.rows.insert(shares.rows.end(),bill.rows.begin(),bill.rows.end());
        }
    }
    if(shares.rows.empty()) shares.rows.push_back("No shared bills yet.");
    if(due.rows.empty()) due.rows.push_back("No pending payments.");
    TerminalUI::cards({{"You owe",{TerminalUI::money(outgoing)},"31"},{"Owed to you",{TerminalUI::money(incoming)},"32"}},TerminalUI::contentWidth());
    TerminalUI::cards({due,shares},TerminalUI::contentWidth());
    vector<string> options{"Split a bill","Receive repayment"};
    if(outgoing.raw()) options.push_back("Pay pending bill");
    int mode=TerminalUI::actions("Bill actions",options);
    if(mode==1) split(w);
    if(mode==2) {
        if(w.shares.empty()) throw invalid_argument("Create a split first.");
        auto& s=w.shares[TerminalUI::numberField("Share number",1,int(w.shares.size()))-1];
        if(s.receiptId) throw invalid_argument("Already received.");
        Entry e;
        e.date=TerminalUI::dateField("Received date");
        if(e.date<w.at(s.expenseId).entry.date) throw invalid_argument("Receipt precedes expense.");
        e.time=readTime(); e.method=TerminalUI::pick("Payment method",methods);
        e.category="Reimbursement"; e.description=s.name+" / "+s.note+" / bill #"+to_string(s.expenseId);
        e.amount=s.amount; s.receiptId=w.add('I',move(e));
        TerminalUI::cards({{"Repayment received",{"Income #"+to_string(s.receiptId)+" · "+TerminalUI::money(s.amount)},"32"}},TerminalUI::contentWidth());
    }
    if(mode==3) {
        auto& e=w.at(TerminalUI::numberField("Pending record ID",1,999999999)).entry;
        if(e.due.empty()) throw invalid_argument("Record is already posted.");
        auto d=TerminalUI::dateField("Actual payment date");
        if(d<e.date) throw invalid_argument("Payment precedes record date.");
        auto time=readTime();
        e.date=d; e.time=time; e.due.clear();
        TerminalUI::cards({{"Bill paid",{"Expense #"+to_string(e.id)+" · "+TerminalUI::money(e.amount)},"32"}},TerminalUI::contentWidth());
    }
}

// Updates budgets, category priorities and expense patterns.
void MoneyMapApp::settings(Wallet& w) {
    TerminalUI::screen("Budgets & categories",16);
    TerminalUI::cards({{"Budget limits",{"[1] Category limit","[4] Whole-month limit"},"33"},
               {"Organize expenses",{"[2] Category priority","[3] Change category","[5] Tag expense pattern"},"36"}},TerminalUI::contentWidth());
    cout<<TerminalUI::margin()<<"[0] Back\n";
    int n=TerminalUI::numberField("Planning action",0,5);
    if(!n) return;
    if(n==5) {
        auto& t=w.at(TerminalUI::numberField("Expense ID",1,999999999));
        if(t.kind()!='E') throw invalid_argument("Select an expense record.");
        t.entry.pattern=TerminalUI::pick("Expense pattern",{"Variable","Fixed","Recurring fixed","Recurring variable","Subscription"});
        return;
    }
    if(n==4) {auto m=TerminalUI::monthField(); w.budgets[m+"|*"]=TerminalUI::amountField("Monthly budget"); return;}
    string c=TerminalUI::pick("Choose a category",categories);
    if(n==1) {
        auto m=TerminalUI::monthField();
        auto cap=TerminalUI::amountField("Monthly limit");
        w.budgets[m+"|"+c]=cap;
    } else if(n==2) {
        cout << "Current priority " << InputTools::priority(w,c) << '\n';
        w.priorities[c]=TerminalUI::pick("Category priority",{"Want","Need","Must"});
    } else {
        auto& t=w.at(TerminalUI::numberField("Expense ID",1,999999999));
        if(t.kind()!='E') throw invalid_argument("Select an expense.");
        t.entry.category=c;
    }
}

// Loads the active wallet and runs the dashboard action loop.
int MoneyMapApp::run() {
    try {
        openSession();
        bool firstRun=!filesystem::exists("wallet.dat");
        w.load();
        if(w.owner.empty()) {
            for(;;) {
                try {if(firstRun) w=Wallet(); setup(w,firstRun); break;}
                catch(const invalid_argument& err) {cout << err.what() << "\nPlease retry setup.\n";}
            }
        } else {
            for(;;) {
                try {if(!accounts(w,false)) return 0; break;}
                catch(const invalid_argument& err) {cout<<err.what()<<'\n'; TerminalUI::pause();}
            }
        }
        for(;;) {
            reports.dashboard(w);

            int n;
            try {
                string cmd=InputTools::lower(InputTools::prompt("Action"));
                const string shortcuts="qnhabgsipuer";
                auto key=cmd.size()==1 ? shortcuts.find(cmd[0]) : string::npos;
                if(key!=string::npos) n=int(key);
                else if(cmd.size()==1 && cmd[0]>='0' && cmd[0]<='9') n=cmd[0]-'0';
                else throw invalid_argument("Use a shortcut shown on the dashboard.");
                TerminalUI::clear();
                if(!n) break;
                switch(n) {
                case 1: addManual(w); break;
                case 2:
                    if(TerminalUI::pick("Wallet history · Period",{"All","Month"})=="All") records(w); else records(w,TerminalUI::monthField());
                    continue;
                case 3: {
                    TerminalUI::screen("Statistics period",12);
                    auto period=InputTools::lower(TerminalUI::formField("Report period","Enter for "+InputTools::localStamp("%B %Y")+" · or type YYYY-MM or all",true));
                    if(period.empty()) period=InputTools::localStamp("%Y-%m");
                    if(period!="all" && !InputTools::validDate(period+"-01")) throw invalid_argument("Invalid month.");
                    reports.report(w,period=="all"?"":period);
                    break;
                }
                case 4: settings(w); break;
                case 5: goals(w); break;
                case 6: payments(w); break;
                case 7: bank.menu(w); break;
                case 8: openingBalances(w,false); break;
                case 9: accounts(w); continue;
                case 10: manageExpense(w); break;
                case 11: reports.fullReport(w); continue;
                }
            } catch(const invalid_argument& err) {TerminalUI::cards({{"Check your entry",{err.what()},"33"}},min(TerminalUI::contentWidth(),76)); TerminalUI::pause(); continue;}
            if(n!=2 && n!=3 && n!=7) w.save();
            TerminalUI::pause();
        }
    } catch(const ios_base::failure& err) {
        if(cin.eof()) return 0;
        cerr << "File error: " << err.what() << '\n'; return 1;
    } catch(const exception& err) {cerr << err.what() << '\n'; return 1;}
    return 0;
}
