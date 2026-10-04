#include "../Source/GeometricWarfare/Simulation/DamageNumbers.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace gw;
static int assertions=0;
static void check(bool ok,const char* message){++assertions;if(!ok)throw std::runtime_error(message);}
static bool close(double a,double b){return std::abs(a-b)<1e-6;}
int main(){
    try{
        DamageNumbers pool;auto* original=pool.slots.data();
        pool.add({{100,200},15,2,1,42});
        check(pool.activeCount()==1&&close(pool.slots[0].amount,15),"damage uses one pool slot");
        pool.step(.1);const double alpha=pool.slots[0].alpha(),rise=pool.slots[0].rise();
        check(alpha<1&&alpha>0&&rise>0,"age fades and raises damage independent of gameplay");
        pool.add({{110,210},7.5,2,1,42});
        check(pool.activeCount()==1&&close(pool.slots[0].amount,22.5)&&close(pool.slots[0].age,.1),"same target within short window aggregates without extending lifetime");
        pool.add({{110,210},2,0,2,42});check(pool.activeCount()==2,"NPC and player equal numeric id do not merge");
        pool.step(.11);pool.add({{120,220},5,2,1,42});check(pool.activeCount()==3,"later damage gets its own rising number");
        pool.step(DamageNumbers::Lifetime);check(pool.activeCount()==0,"large display delta expires all numbers without stale UI");
        pool.add({{0,0},0,1,1,1});pool.add({{0,0},NAN,1,1,1});pool.add({{NAN,0},1,1,1,1});check(pool.activeCount()==0,"invalid visual data is ignored");
        for(int i=0;i<5000;++i)pool.add({{1,2},1,1,1,i});
        check(pool.activeCount()==DamageNumbers::Capacity&&pool.slots.size()==DamageNumbers::Capacity&&pool.slots.data()==original,"5000 hits reuse a fixed bounded pool");
        bool hasNewest=false;for(const auto& slot:pool.slots)hasNewest|=slot.active&&slot.targetId==4999;
        check(hasNewest,"overflow keeps recent damage visible");
        pool.step(-1);pool.step(NAN);check(pool.activeCount()==DamageNumbers::Capacity,"invalid display deltas are ignored");
        pool.reset();check(pool.activeCount()==0&&pool.slots.data()==original,"reset reuses existing allocation");
        pool.add({{10,20},.15,1,1,42});
        pool.add({{10,20},7.25,1,1,42,NumberKind::Healing});
        check(pool.activeCount()==2,"healing and damage for the same target never merge");
        check(pool.slots[0].kind==NumberKind::Damage&&close(pool.slots[0].amount,.15),"damage retains its kind and fractional amount");
        check(pool.slots[1].kind==NumberKind::Healing&&close(pool.slots[1].amount,7.25),"healing slot retains its kind and capped fractional amount");
        pool.step(.1);pool.add({{11,21},.5,1,1,42,NumberKind::Healing});
        check(pool.activeCount()==2&&close(pool.slots[1].amount,7.75)&&close(pool.slots[1].age,.1),"healing merges only with nearby healing without resetting age");
        for(int i=0;i<5000;++i)pool.add({{1,2},.25,1,1,i,i%2?NumberKind::Healing:NumberKind::Damage});
        check(pool.activeCount()==DamageNumbers::Capacity&&pool.slots.size()==DamageNumbers::Capacity&&pool.slots.data()==original,"mixed healing and damage reuse the fixed pool at scale");
        std::cout<<assertions<<" damage-number assertions passed\n";
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
