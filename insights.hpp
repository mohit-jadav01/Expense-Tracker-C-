#pragma once
#include "terminal.hpp"
#include <chrono>
#include <cmath>
#include <set>

// The same dated cash-movement calculations serve the two independent histories.
namespace insights {
using namespace std::chrono;
struct Movement {
    string date,time,label;
    Money amount;
    char kind; // E = out, I = in, T = internal transfer
};
inline sys_days day(const string& iso) {
    return sys_days{year{std::stoi(iso.substr(0,4))}/unsigned(std::stoi(iso.substr(5,2)))/unsigned(std::stoi(iso.substr(8)))};
}
inline string iso(sys_days d) {
    year_month_day v{d};
    std::ostringstream s;
    s<<int(v.year())<<'-'<<std::setfill('0')<<std::setw(2)<<unsigned(v.month())<<'-'<<std::setw(2)<<unsigned(v.day());
    return s.str();
}
inline Money mean(Money sum,int n) {return n?Money(sum.raw()/n):Money();}
inline string change(Money now,Money prior) {
    if(prior.raw()<=0) return "No prior amount";
    std::ostringstream s;
    s<<std::fixed<<std::setprecision(1)<<100.0*(now.raw()-prior.raw())/prior.raw()<<'%';
    return s.str();
}
inline void analyze(const vector<Movement>& rows,const string& month,bool bank) {
    using namespace std;
    string anchor=InputTools::localStamp("%Y-%m-%d");
    if(bank && !rows.empty()) anchor=max_element(rows.begin(),rows.end(),[](auto& a,auto& b){return a.date<b.date;})->date;
    if(!month.empty() && month!=anchor.substr(0,7)) {
        year_month ym=year_month_day(day(month+"-01")).year()/year_month_day(day(month+"-01")).month();
        anchor=iso(sys_days{ym/last});
    }
    sys_days ref=day(anchor);
    year_month_day ymd{ref};
    year_month ym=ymd.year()/ymd.month();
    sys_days monday=ref-days{weekday{ref}.iso_encoding()-1};
    map<string,Money> daily,weekly,monthly,yearly;
    map<string,int> activity;
    Money credit,debit,volume,weekend,weekdayOut,am,pm;
    const Movement *high=nullptr,*low=nullptr;
    int ins=0,outs=0,count=0;
    for(const auto& r:rows) {
        if(!month.empty() && r.date.substr(0,7)!=month) continue;
        ++count; ++activity[r.date]; volume=volume+r.amount;
        if(!high || r.amount>high->amount) high=&r;
        if(!low || low->amount>r.amount) low=&r;
        if(r.kind=='I') {credit=credit+r.amount; ++ins;}
        if(r.kind!='E') continue;
        ++outs; debit=debit+r.amount;
        auto d=day(r.date); int wd=weekday{d}.iso_encoding();
        Money& part=wd>5?weekend:weekdayOut; part=part+r.amount;
        if(!r.time.empty()) {auto& half=r.time<"12:00"?am:pm; half=half+r.amount;}
        for(auto* cell:{&daily[r.date],&weekly[iso(d-days{wd-1})],&monthly[r.date.substr(0,7)],&yearly[r.date.substr(0,4)]})
            *cell=*cell+r.amount;
    }
    vector<TerminalUI::Card> sections;
    ostringstream out;
    auto section=[&](const string& name,const string& accent) {
        vector<string> lines;
        istringstream in(out.str());
        for(string line;getline(in,line);) if(!line.empty()) lines.push_back(line);
        if(!lines.empty()) sections.push_back({name,move(lines),accent});
        out.str(""); out.clear();
    };
    out<<"Average transaction     INR "<<mean(volume,count).str()
        <<"\nAverage incoming        INR "<<mean(credit,ins).str()
        <<"\nAverage outgoing        INR "<<mean(debit,outs).str()<<'\n';
    if(high) out<<"Largest   "<<high->date<<"   "<<high->amount.str()<<"   "<<high->label
                 <<"\nSmallest  "<<low->date<<"   "<<low->amount.str()<<"   "<<low->label<<'\n';
    section("Transactions","36");
    sections.push_back({"Spending habits",{},"97"});
    auto flow=[&](sys_days first,sys_days end,char kind) {
        Money sum; auto a=iso(first),b=iso(end);
        for(const auto& r:rows) if(r.kind==kind && r.date>=a && r.date<b) sum=sum+r.amount;
        return sum;
    };
    auto end=ref+days{1};
    auto current=flow(sys_days{ym/1},end,'E');
    auto previous=flow(sys_days{(ym-months{1})/1},sys_days{ym/1},'E');
    auto week=flow(monday,end,'E'), lastWeek=flow(monday-days{7},monday,'E');
    out<<"\nCalendar as of "<<anchor<<" (current periods through this date)\n"
        <<"Day      "<<flow(ref,end,'E').str()<<"   Previous "<<flow(ref-days{1},ref,'E').str()
        <<"\nWeek     "<<week.str()<<"   Previous "<<lastWeek.str()<<"   Change "<<change(week,lastWeek)
        <<"\nMonth    "<<current.str()<<"   Previous "<<previous.str()<<"   Change "<<change(current,previous)
        <<"\nYear     "<<flow(sys_days{ymd.year()/January/1},end,'E').str()<<"   Previous "
        <<flow(sys_days{(ymd.year()-years{1})/January/1},sys_days{ymd.year()/January/1},'E').str()<<'\n';
    if(!bank) {
        Money net=flow(sys_days{ym/1},end,'I')-current;
        Money prior=flow(sys_days{(ym-months{1})/1},sys_days{ym/1},'I')-previous;
        out<<"Monthly net savings change "<<change(net,prior)<<'\n';
    }
    out<<"Month-end outflow at current pace "<<Money(llround(double(current.raw())/unsigned(ymd.day())*
         unsigned(year_month_day{sys_days{ym/last}}.day()))).str()<<'\n';
    section("Calendar comparison","33");
    if(bank) {
        // Flag unusually large debits against the same named counterparty's history.
        vector<Movement> history;
        for(const auto& r:rows) if(r.kind=='E' && r.label!="Unidentified") history.push_back(r);
        stable_sort(history.begin(),history.end(),[](const auto& a,const auto& b){return a.date<b.date;});
        map<string,vector<Money>> prior;
        map<pair<string,Cents>,set<string>> monthsPaid;
        for(const auto& r:history) {
            auto& prev=prior[r.label];
            bool selected=month.empty() || r.date.substr(0,7)==month;
            if(selected && prev.size()>=3 && r.amount.raw()>100000) {
                auto n=prev.size();
                Money three=prev[n-1]+prev[n-2]+prev[n-3];
                if(r.amount.raw()>=three.raw())
                    out<<"Review debit  "<<TerminalUI::shortDate(r.date)<<"  "<<r.label<<"  "<<TerminalUI::money(r.amount)
                        <<"   Previous three average "<<TerminalUI::money(mean(three,3))<<'\n';
            }
            prev.push_back(r.amount);
            monthsPaid[{r.label,r.amount.raw()}].insert(r.date.substr(0,7));
        }
        for(const auto& [key,months]:monthsPaid)
            if(months.size()>=3 && (month.empty() || months.count(month)))
                out<<"Repeated debit  "<<key.first<<"  "<<TerminalUI::money(Money(key.second))
                    <<" in "<<months.size()<<" distinct months\n";
    }
    section("Payments to review","33");
    auto bars=[&](const string& label,const map<string,Money>& data){
        if(data.empty()) return;
        Money peak; for(auto& p:data) if(p.second>peak) peak=p.second;

        for(const auto& [key,value]:data) out<<InputTools::column(key,12)<<TerminalUI::meter(double(value.raw())/peak.raw(),16)<<"  "<<value.str()<<'\n';
        section(label,"32");
    };
    bars("Daily outflow",daily); bars("Weekly outflow (Monday)",weekly);
    bars("Monthly outflow",monthly); bars("Yearly outflow",yearly);
    for(;;) {
        TerminalUI::clear(); TerminalUI::title(bank?"Bank statement trends":"Wallet trends");
        TerminalUI::cards({{"Received",{"▲ "+TerminalUI::money(credit),to_string(ins)+" incoming"},"32"},
                   {"Paid out",{"▼ "+TerminalUI::money(debit),to_string(outs)+" outgoing"},"31"},
                   {"Net movement",{TerminalUI::money(credit-debit),to_string(count)+" transactions"},"36"}},TerminalUI::contentWidth());
        vector<string> choices;
        for(size_t i=0;i<sections.size();++i) choices.push_back(to_string(i+1)+"  "+sections[i].title);
        choices.push_back("0  Back"); TerminalUI::cards({{"Explore trends",choices,"35"}},TerminalUI::contentWidth());
        int n=TerminalUI::numberField("Choose a trend",0,int(sections.size())); if(!n) return;
        const auto& chosen=sections[n-1];
        if(chosen.title=="Transactions") {
            TerminalUI::screen("Transaction amounts",22);
            TerminalUI::cards({{"Average transaction",{TerminalUI::money(mean(volume,count))},"97"},
                {"Average received",{TerminalUI::money(mean(credit,ins))},"32"},
                {"Average paid out",{TerminalUI::money(mean(debit,outs))},"31"}},TerminalUI::contentWidth());
            vector<TerminalUI::Card> extremes;
            for(auto [label,item]:vector<pair<string,const Movement*>>{{"Largest",high},{"Smallest",low}})
                if(item) extremes.push_back({label,{TerminalUI::money(item->amount),TerminalUI::shortDate(item->date)+" "+item->date.substr(0,4),item->label},"36"});
            if(extremes.empty()) extremes.push_back({"Transactions",{"No transactions in this period"},"90"});
            TerminalUI::cards(extremes,TerminalUI::contentWidth());
            InputTools::prompt("Enter to return",true);
            continue;
        }
        if(chosen.title=="Calendar comparison") {
            vector<vector<string>> comparison;
            auto add=[&](const string& label,Money now,Money prior) {
                comparison.push_back({label,TerminalUI::money(now),TerminalUI::money(prior),change(now,prior)});
            };
            add("Day",flow(ref,end,'E'),flow(ref-days{1},ref,'E'));
            add("Week",week,lastWeek);
            add("Month",current,previous);
            add("Year",flow(sys_days{ymd.year()/January/1},end,'E'),
                flow(sys_days{(ymd.year()-years{1})/January/1},sys_days{ymd.year()/January/1},'E'));
            Money forecast(llround(double(current.raw())/unsigned(ymd.day())*
                unsigned(year_month_day{sys_days{ym/last}}.day())));
            comparison.push_back({"Month-end estimate",TerminalUI::money(forecast),"At current pace",""});
            if(!bank) comparison.push_back({"Net savings change","","",
                change(flow(sys_days{ym/1},end,'I')-current,
                    flow(sys_days{(ym-months{1})/1},sys_days{ym/1},'I')-previous)});
            TerminalUI::table("Calendar comparison · through "+TerminalUI::shortDate(anchor)+" "+anchor.substr(0,4),
                {{"Period",3},{"Current",2,"97",true},{"Previous",2,"37",true},{"Change",2,"36",true}},comparison);
            continue;
        }
        if(chosen.title.find("outflow")!=string::npos) {
            const auto& values=chosen.title=="Daily outflow"?daily:chosen.title=="Weekly outflow (Monday)"?weekly:
                chosen.title=="Monthly outflow"?monthly:yearly;
            Money peak;
            for(const auto& [date,amount]:values) if(amount>peak) peak=amount;
            vector<vector<string>> chart;
            for(const auto& [date,amount]:values) {
                string period=date.size()==10?TerminalUI::shortDate(date)+" "+date.substr(0,4):date;
                chart.push_back({period,TerminalUI::money(amount),TerminalUI::meter(double(amount.raw())/peak.raw(),16)});
            }
            TerminalUI::table(chosen.title,{{"Period",3},{"Paid out",2,"31",true},{"Compared to peak",4,"36"}},chart);
            continue;
        }
        if(chosen.title!="Spending habits") {TerminalUI::browse(chosen.title,chosen.rows,chosen.accent);continue;}
        TerminalUI::clear();TerminalUI::title("Spending habits · "+(month.empty()?string("All dates"):month));
        int width=TerminalUI::contentWidth();
        TerminalUI::cards({{"Per active day",{"▼ "+TerminalUI::money(mean(debit,int(daily.size())))}},
                   {"Per active week",{"▼ "+TerminalUI::money(mean(debit,int(weekly.size())))}},
                   {"Per active month",{"▼ "+TerminalUI::money(mean(debit,int(monthly.size())))}}},width);
        TerminalUI::Card timing{"Weekly rhythm",{"Weekdays   "+TerminalUI::money(weekdayOut),"Weekend    "+TerminalUI::money(weekend)}};
        TerminalUI::Card timeOfDay{"Time of day",{"Before noon    "+TerminalUI::money(am),"Noon onward    "+TerminalUI::money(pm)}};
        if(bank) timeOfDay={"Money movement",{credit.raw()?"Paid out / received   "+TerminalUI::decimal(100.*debit.raw()/credit.raw())+"%":"No incoming payments"}};
        TerminalUI::cards({timing,timeOfDay},width);
        TerminalUI::Card daysCard{"Activity highlights",{}},consistency{"Spending consistency",{}};
        if(!activity.empty()) {
            auto peak=max_element(activity.begin(),activity.end(),[](auto& a,auto& b){return a.second<b.second;});
            daysCard.rows.push_back("Most active   "+TerminalUI::shortDate(peak->first)+" · "+to_string(peak->second)+" entries");
        }
        if(!daily.empty()) {
            auto [lo,hi]=minmax_element(daily.begin(),daily.end(),[](auto& a,auto& b){return b.second>a.second;});
            daysCard.rows.push_back("Highest   "+TerminalUI::shortDate(hi->first)+" · "+TerminalUI::money(hi->second));
            daysCard.rows.push_back("Lowest    "+TerminalUI::shortDate(lo->first)+" · "+TerminalUI::money(lo->second));
            auto span=(day(activity.rbegin()->first)-day(activity.begin()->first)).count()+1;
            if(span>=3) {
                long double avg=static_cast<long double>(debit.raw())/span,squares=0;
                for(const auto& item:daily) squares+=static_cast<long double>(item.second.raw())*item.second.raw();
                auto score=1/(1+sqrt(max(0.0L,squares/span-avg*avg))/avg);
                consistency.rows={TerminalUI::meter(double(score),10)+" "+to_string(lround(100*score))+"/100","Over "+to_string(span)+" calendar days"};
            }
        }
        if(daysCard.rows.empty()) daysCard.rows={"No activity in this period."};
        if(consistency.rows.empty()) consistency.rows={"Needs spending and 3 days of activity."};
        if(!bank && credit.raw()) consistency.rows.push_back("Outflow / income   "+TerminalUI::decimal(100.*debit.raw()/credit.raw())+"%");
        TerminalUI::cards({daysCard,consistency},width);
        InputTools::prompt("Enter to return",true);
    }
}
}
