#include "reports.hpp"

// Displays all wallet details in a continuous report.
void WalletReports::fullReport(const Wallet& w) {
    TerminalUI::clear();
    TerminalUI::title(w.owner+" · Complete wallet report");
    const int width=TerminalUI::contentWidth();
    auto section=[&](const string& title,const vector<string>& lines,const string& tint) {
        vector<string> wrapped;
        for(auto line:lines) {
            while(TerminalUI::columns(line)>width-4) {
                auto end=TerminalUI::prefix(line,width-4);
                wrapped.push_back(line.substr(0,end));
                line.erase(0,end);
            }
            wrapped.push_back(line);
        }
        if(wrapped.empty()) wrapped.push_back("No records yet.");
        TerminalUI::cards({{title,wrapped,tint}},width);
    };
    Money balance=w.opening,income,spent,reserved,owed,pending;
    map<string,Money> categories,priorities;
    for(const auto& tx:w.ledger) {
        const auto& e=tx->entry;
        if(!e.due.empty()) {pending=pending+e.amount;continue;}
        balance=balance+tx->impact();
        if(tx->kind()=='I') income=income+e.amount;
        if(tx->kind()=='E') {
            spent=spent+e.amount;
            categories[e.category]=categories[e.category]+e.amount;
            priorities[InputTools::priority(w,e.category)]=priorities[InputTools::priority(w,e.category)]+e.amount;
        }
    }
    for(const auto& g:w.goals) reserved=reserved+g.saved;
    for(const auto& s:w.shares) if(!s.receiptId) owed=owed+s.amount;
    section("Account summary · All dates",{"Prepared "+InputTools::localStamp("%d %b %Y")+" · "+w.owner,
        "Opening balance   "+TerminalUI::money(w.opening),"▲ Income received   "+TerminalUI::money(income),
        "▼ Expenses paid   "+TerminalUI::money(spent),"Net cash flow   "+TerminalUI::money(income-spent),
        "Current balance   "+TerminalUI::money(balance),"Reserved for goals   "+TerminalUI::money(reserved),
        "Available after goal reservations   "+TerminalUI::money(balance-reserved)},"97");
    for(const auto& [name,tint]:vector<pair<string,string>>{{"Must","36"},{"Need","34"},{"Want","35"}}) {
        double share=spent.raw()?100.0*priorities[name].raw()/spent.raw():0;
        section(name+" spending",{TerminalUI::money(priorities[name])+" · "+TerminalUI::decimal(share)+"% of expenses"},tint);
    }
    for(const auto& [name,amount]:categories)
        section(name+" · "+InputTools::priority(w,name),{TerminalUI::money(amount)},"33");
    vector<string> lines;
    for(const auto& g:w.goals) {
        double ratio=g.target.raw()?double(g.saved.raw())/g.target.raw():0;
        lines.push_back(g.name+" · "+TerminalUI::money(g.saved)+" / "+TerminalUI::money(g.target));
        lines.push_back(TerminalUI::meter(ratio,18)+" "+TerminalUI::decimal(100*ratio)+"% complete");
    }
    section("Savings goals",lines,"32");
    lines.clear();
    for(const auto& [key,cap]:w.budgets) {
        Money used;
        for(const auto& tx:w.ledger) {
            const auto& e=tx->entry;
            if(tx->kind()=='E' && e.due.empty() && e.date.substr(0,7)==key.substr(0,7) &&
               (key.substr(8)=="*" || key.substr(8)==e.category)) used=used+e.amount;
        }
        lines.push_back(key.substr(0,7)+" · "+(key.substr(8)=="*"?"Whole wallet":key.substr(8)));
        lines.push_back("Limit "+TerminalUI::money(cap)+" · used "+TerminalUI::money(used)+" · remaining "+TerminalUI::money(cap-used));
    }
    section("Budget history",lines,"33");
    lines={"Awaiting repayment   "+TerminalUI::money(owed),"Pending bills   "+TerminalUI::money(pending)};
    for(const auto& s:w.shares)
        lines.push_back("Bill #"+to_string(s.expenseId)+" · "+s.name+" · "+TerminalUI::money(s.amount)+" · "+
                        (s.receiptId?"Received":"Outstanding")+" · "+s.note);
    section("Split bills and repayments",lines,"36");
    for(const auto& tx:w.ledger) {
        const auto& e=tx->entry;
        string kind=tx->kind()=='E'?"Expense":tx->kind()=='I'?"Income":"Transfer";
        vector<string> detail{TerminalUI::money(e.amount)+" · "+e.date+" "+e.time,
            "Category   "+e.category+" · "+InputTools::priority(w,e.category),"Payment method   "+e.method,
            "Description   "+(e.description.empty()?"None":e.description)};
        if(!e.pattern.empty()) detail.push_back("Pattern   "+e.pattern);
        if(!e.destination.empty()) detail.push_back("From "+e.account+" to "+e.destination);
        if(!e.due.empty()) detail.push_back("Pending · due "+e.due);
        section("#"+to_string(e.id)+" · "+kind,detail,tx->kind()=='E'?"31":tx->kind()=='I'?"32":"36");
    }
    if(w.ledger.empty()) section("Transactions",{},"90");
    Money bankIn,bankOut;
    for(const auto& row:w.statement) {
        if(row.debit) bankOut=bankOut+row.amount;else bankIn=bankIn+row.amount;
    }
    section("Imported bank statement",{to_string(w.statement.size())+" stored rows",
        "▲ Received   "+TerminalUI::money(bankIn),"▼ Paid out   "+TerminalUI::money(bankOut),
        "Open [I] Import for individual statement transactions and merchants."},"36");
    cout<<TerminalUI::margin();InputTools::prompt("End of report · Enter to return",true);
}

// Calculates a percentage with a zero-denominator guard.
double WalletReports::percent(Money value,Money base) {return base.raw()?100.0*value.raw()/base.raw():0;}

// Accumulates wallet totals using virtual transaction behavior.
WalletReports::Summary WalletReports::summarize(const Wallet& w,const string& month) {
    Summary s;
    s.balance=w.opening;
    for(const auto& tx:w.ledger) {
        const Entry& e=tx->entry;
        if(!e.due.empty()) continue;
        Money impact=tx->impact(); // Runtime dispatch through Transaction.
        s.balance=s.balance+impact;
        if(!month.empty() && e.date.substr(0,7)!=month) continue;
        s.net=s.net+impact;
        ++s.methodCount[e.method];
        if(tx->kind()=='I') {s.income=s.income+e.amount; ++s.incomes;}
        else if(tx->kind()=='T') ++s.transfers;
        else {
            ++s.expenses;
            s.expense=s.expense+e.amount;
            for(auto* total:{&s.category[e.category],&s.day[e.date],&s.byPriority[InputTools::priority(w,e.category)]})
                *total=*total+e.amount;
            ++s.categoryCount[e.category];
            if(!s.largest || e.amount>s.largest->amount) s.largest=&e;
            if(!s.smallest || s.smallest->amount>e.amount) s.smallest=&e;
        }
    }
    return s;
}

// Finds unusual expenses relative to previous spending.
vector<string> WalletReports::flags(const Wallet& w,const string& month) {
    vector<string> rows;
    vector<const Entry*> sorted;
    for(const auto& tx:w.ledger) if(tx->kind()=='E' && tx->entry.due.empty()) sorted.push_back(&tx->entry);
    stable_sort(sorted.begin(),sorted.end(),[](auto a,auto b){
        return tie(a->date,a->time,a->id)<tie(b->date,b->time,b->id);
    });
    map<string,vector<Money>> history;

    for(auto e:sorted) {
        auto& past=history[e->category];
        if((month.empty() || e->date.substr(0,7)==month) && past.size()>=3 && e->amount.raw()>100000) {
            auto n=past.size();
            Money total=past[n-1]+past[n-2]+past[n-3];
            if(e->amount.raw()>=total.raw()) {
                rows.push_back("#"+to_string(e->id)+"  "+e->category+"  INR "+e->amount.str());
                rows.push_back("Previous three: "+past[n-3].str()+", "+past[n-2].str()+", "+past[n-1].str());
            }
        }
        past.push_back(e->amount);
    }
    if(rows.empty()) rows.push_back("No unusual expenses flagged.");
    return rows;
}

// Displays the current wallet overview and shortcuts.
void WalletReports::dashboard(const Wallet& w) {
    TerminalUI::clear();
    cout<<string(std::max(0,(TerminalUI::viewport().second-34)/2),'\n');
    int width=TerminalUI::contentWidth();
    string month=InputTools::localStamp("%Y-%m");
    auto s=summarize(w,month);
    TerminalUI::title(w.owner+" · "+InputTools::localStamp("%B %Y"));
    TerminalUI::cards({{"Balance",{TerminalUI::money(s.balance)},s.balance.raw()<0?"31":"97"},
               {"Income",{"▲ "+TerminalUI::money(s.income)},"32"},
               {"Spent",{"▼ "+TerminalUI::money(s.expense)},"31"}},width);
    vector<TerminalUI::Card> mix;
    for(const auto& [name,tint]:vector<pair<string,string>>{{"Must","36"},{"Need","34"},{"Want","35"}})
        mix.push_back({name,{TerminalUI::decimal(percent(s.byPriority[name],s.expense))+"% of spending",
            TerminalUI::money(s.byPriority[name])+" spent"},tint});
    TerminalUI::cards(mix,width);

    Money cap,used;
    auto budget=w.budgets.find(month+"|*");
    if(budget!=w.budgets.end()) {cap=budget->second; used=s.expense;}
    else for(const auto& [key,value]:w.budgets) if(key.substr(0,7)==month) {
        cap=cap+value; used=used+s.category[key.substr(8)];
    }
    TerminalUI::Card plan{"Monthly budget",{"No budget yet · press [B]"},"90"};
    if(cap.raw()) {
        double ratio=double(used.raw())/cap.raw();
        ostringstream percent;
        percent<<fixed<<setprecision(0)<<ratio*100<<"% used";
        plan.rows={TerminalUI::meter(ratio,10)+"  "+percent.str(),TerminalUI::money(used)+" of "+TerminalUI::money(cap)};
        plan.accent=ratio>=1?"31":ratio>0.8?"33":"97";
    }
    TerminalUI::Card goal{"Savings goals",{"No goals yet · press [G]"},"90"};
    if(!w.goals.empty()) {
        auto it=find_if(w.goals.begin(),w.goals.end(),[](auto& g){return g.target>g.saved;});
        const auto& g=it!=w.goals.end()?*it:w.goals.front();
        double ratio=double(g.saved.raw())/g.target.raw();
        goal.rows={g.name,TerminalUI::meter(ratio,10)+" "+TerminalUI::decimal(100*ratio)+"%",TerminalUI::money(g.saved)+" / "+TerminalUI::money(g.target)};
        goal.accent="32";
    }
    TerminalUI::cards({plan,goal},width);
    TerminalUI::Card recent{"Recent transactions",{}};
    int inner=width-4;
    bool wide=inner>=52;
    int amountWidth=16;
    for(auto it=w.ledger.rbegin();it!=w.ledger.rend() && recent.rows.size()<3;++it) {
        const auto& t=**it; const auto& e=t.entry;
        string name=e.description.empty()?e.category:e.description;
        string value=t.kind()=='I'?"▲ +"+TerminalUI::money(e.amount):t.kind()=='E'?"▼ -"+TerminalUI::money(e.amount):"↔ "+TerminalUI::money(e.amount);
        if(!e.due.empty()) value="Due "+TerminalUI::money(e.amount);
        int aw=min(inner,max(amountWidth,TerminalUI::columns(value)));
        string row;
        if(wide) row=TerminalUI::fit(TerminalUI::shortDate(e.date),8)+TerminalUI::fit(name,inner-8-15-aw)+TerminalUI::fit(e.category,15);
        else row=TerminalUI::fit(TerminalUI::shortDate(e.date)+" "+name,inner-aw);
        recent.rows.push_back(row+TerminalUI::fit(value,aw,true));
    }
    if(recent.rows.empty()) recent.rows={"Add your first transaction with [N]."};
    TerminalUI::cards({recent},width);
    Money owed;
    for(const auto& share:w.shares) if(!share.receiptId) owed=owed+share.amount;
    if(owed.raw()) cout<<TerminalUI::margin()<<"Awaiting repayment  "<<TerminalUI::money(owed)<<'\n';
    const vector<pair<char,string>> shortcuts{{'N',"Add"},{'H',"History"},{'E',"Edit"},{'A',"Stats"},{'R',"Full report"},
        {'B',"Budgets"},{'G',"Goals"},{'S',"Split"},{'I',"Import"},{'U',"Accounts"},{'P',"Opening bal."},{'Q',"Quit"}};
    int usedCols=0;
    cout<<'\n'<<TerminalUI::margin();
    for(const auto& [key,label]:shortcuts) {
        int len=4+TerminalUI::columns(label);
        if(usedCols && usedCols+2+len>width){cout<<'\n'<<TerminalUI::margin();usedCols=0;}
        if(usedCols){cout<<"  ";usedCols+=2;}
        cout<<TerminalUI::paint("["+string(1,key)+"]","1;36")<<" "<<label;usedCols+=len;
    }
    cout<<'\n'<<TerminalUI::margin();
}

// Displays budget usage for the selected month.
void WalletReports::budgetReport(const Wallet& w,const string& month,const Summary& s) {
    vector<vector<string>> rows;
    Money limit,used; int count=0,over=0;
    for(const auto& [key,cap]:w.budgets) {
        if(key.substr(0,7)!=month) continue;
        string c=key.substr(8);
        Money spent=c=="*"?s.expense:s.category.count(c)?s.category.at(c):Money();
        auto add=[&](const string& label,Money total,Money cost) {
            double ratio=percent(cost,total);
            rows.push_back({label,TerminalUI::money(total),TerminalUI::money(cost),TerminalUI::money(total-cost),TerminalUI::meter(ratio/100,10)+" "+TerminalUI::decimal(ratio)+"%"});
        };
        add(c=="*"?"Whole wallet":c,cap,spent);
        if(c!="*"){limit=limit+cap;used=used+spent;++count;if(spent>cap)++over;}
    }
    if(count) rows.push_back({"Category totals",TerminalUI::money(limit),TerminalUI::money(used),TerminalUI::money(limit-used),to_string(over)+" over limit · avg "+TerminalUI::money(Money(used.raw()/count))});
    if(rows.empty()) {
        TerminalUI::screen("Budget progress",12);
        TerminalUI::cards({{"Monthly budget",{month.empty()?"Select a month to review its budget.":"No budget yet · press [B] on the dashboard."}}},std::min(TerminalUI::contentWidth(),68));
        cout<<TerminalUI::inset(std::min(TerminalUI::contentWidth(),68));InputTools::prompt("Enter to return",true);return;
    }
    TerminalUI::table("Budget progress · "+month,{{"Budget",3},{"Limit",2,"97",true},{"Spent",2,"31",true},{"Remaining",2,"97",true},{"Usage",4,"33"}},rows);
}

// Runs the wallet statistics views.
void WalletReports::report(const Wallet& w,const string& m) {
    for(;;) {
    TerminalUI::clear();
    TerminalUI::title(w.owner+"   /   Spending insights   /   "+(m.empty()?string("All dates"):m));
    auto s=summarize(w,m);
    TerminalUI::cards({{"Balance",{TerminalUI::money(s.balance)},"97"},{"Income",{"▲ "+TerminalUI::money(s.income)},"32"},
               {"Spent",{"▼ "+TerminalUI::money(s.expense)},"31"}},TerminalUI::contentWidth());
    vector<string> ranked;
    for(const auto& [c,v]:s.category) if(v.raw()) ranked.push_back(c);
    stable_sort(ranked.begin(),ranked.end(),[&](auto& a,auto& b){return s.category.at(a)>s.category.at(b);});
    vector<string> mix;
    for(const auto& category:ranked) {
        Money amount=s.category.at(category);
        mix.push_back(category+" · "+InputTools::priority(w,category));
        mix.push_back(TerminalUI::meter(percent(amount,s.expense)/100,12)+"  "+
            TerminalUI::decimal(percent(amount,s.expense))+"%   "+TerminalUI::money(amount));
    }
    if(mix.empty()) mix.push_back("No expenses in this period");
    TerminalUI::cards({{"Category spending",mix,"36"}},TerminalUI::contentWidth());
    TerminalUI::cards({{"Explore",{"1  Cash flow and categories","2  Spending trends","3  Budgets","4  Patterns and unusual spending","0  Back"},"35"}},TerminalUI::contentWidth());
    int view=TerminalUI::numberField("Choose a report",0,4);
    if(!view) return;
    if(view==3) {budgetReport(w,m,s);continue;}
    if(view==1) {
        Money reserved;for(const auto& g:w.goals) reserved=reserved+g.saved;
        TerminalUI::clear();TerminalUI::title("Cash flow · "+(m.empty()?string("All dates"):m));
        int width=TerminalUI::contentWidth();
        TerminalUI::cards({{"Net cash flow",{(s.net.raw()<0?"▼ ":s.net.raw()>0?"▲ ":"")+TerminalUI::money(s.net)},"97"},
                   {"Saved in goals",{TerminalUI::money(reserved)},"97"},
                   {"Savings rate",{s.income.raw()?TerminalUI::decimal(percent(s.net,s.income))+"%":"No income recorded"},"97"}},width);
        TerminalUI::cards({{"Activity",{to_string(s.incomes)+" income · "+to_string(s.expenses)+" expenses",to_string(s.transfers)+" transfers"}},
                   {"Expense size",{s.expenses?"Average  "+TerminalUI::money(insights::mean(s.expense,s.expenses)):"No expenses",
                    s.expenses?"High  "+TerminalUI::money(s.largest->amount)+"  #"+to_string(s.largest->id):"",
                    s.expenses?"Low   "+TerminalUI::money(s.smallest->amount)+"  #"+to_string(s.smallest->id):""}}},width);
        vector<TerminalUI::Card> categoryCards;
        for(const auto& c:ranked) {
            int n=s.categoryCount[c];
            string group=InputTools::priority(w,c);
            string tint=group=="Must"?"36":group=="Need"?"34":"35";
            categoryCards.push_back({c+" · "+InputTools::priority(w,c),{TerminalUI::money(s.category[c]),
                TerminalUI::meter(percent(s.category[c],s.expense)/100,12)+" "+TerminalUI::decimal(percent(s.category[c],s.expense))+"%",
                to_string(n)+(n==1?" payment":" payments")+" · avg "+TerminalUI::money(insights::mean(s.category[c],n))},tint});
        }
        for(size_t i=0;i<categoryCards.size();i+=2)
            TerminalUI::cards(vector<TerminalUI::Card>(categoryCards.begin()+i,categoryCards.begin()+min(i+2,categoryCards.size())),width);
        TerminalUI::Card methodsCard{"Payment methods",{},"36"};
        for(auto& [method,n]:s.methodCount) methodsCard.rows.push_back(method+"   "+to_string(n)+(n==1?" transaction":" transactions"));
        if(categoryCards.empty()) TerminalUI::cards({{"Category breakdown",{"No spending this period."},"90"}},width);
        if(methodsCard.rows.empty()) methodsCard.rows.push_back("No payments this period.");
        TerminalUI::cards({methodsCard},width);InputTools::prompt("Enter to return",true);continue;
    }
    if(view==4) {
        map<string,Money> patterns;
        Money fixed,variable,recurring;
        for(auto& tx:w.ledger) {
            auto& e=tx->entry;
            if(tx->kind()!='E'||!e.due.empty()||(!m.empty() && e.date.substr(0,7)!=m)) continue;
            string p=e.pattern.empty()?"Untagged":e.pattern;patterns[p]=patterns[p]+e.amount;
            if(p=="Fixed"||p=="Recurring fixed"||p=="Subscription") fixed=fixed+e.amount;
            if(p=="Variable"||p=="Recurring variable") variable=variable+e.amount;
            if(p=="Subscription"||p.find("Recurring")==0) recurring=recurring+e.amount;
        }
        vector<string> rows{"Fixed   "+TerminalUI::money(fixed),"Variable   "+TerminalUI::money(variable),"Recurring   "+TerminalUI::money(recurring)};
        for(auto& [p,v]:patterns) rows.push_back(p+"   "+TerminalUI::money(v));
        for(auto& [p,v]:s.byPriority) rows.push_back(p+"   "+TerminalUI::money(v));
        auto reviews=flags(w,m);rows.insert(rows.end(),reviews.begin(),reviews.end());
        TerminalUI::browse("Spending patterns",rows,"35");continue;
    }
    vector<insights::Movement> data;
    for(const auto& tx:w.ledger) {
        const auto& e=tx->entry;
        if(e.due.empty()) data.push_back({e.date,e.time,e.description.empty()?e.category:e.description,e.amount,tx->kind()});
    }
    insights::analyze(data,m,false);
    }
}
