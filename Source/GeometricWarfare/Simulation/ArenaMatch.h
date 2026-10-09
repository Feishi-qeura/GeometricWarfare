#pragma once
#include "ArenaPhysics.h"
#include "DamageNumbers.h"
#include "ArenaEvolution.h"
#include "ArenaBoss.h"
#include "ArenaWeapons.h"
#include "ArenaPickups.h"
#include "ArenaAudio.h"
#include <array>
#include <cstdint>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace gw {
enum class Phase { Battle, Sprint, Results };
enum class EventKind { Joined, TeamChanged, ShapeChanged, Kill, Respawn, BaseDestroyed, BaseRebuilt, SprintStarted, RoundEnded, RoundStarted, OrbCollected, NpcKilled, WeaponSwitched, EvolutionObtained, WeaponObtained, BossReward };
struct Event { EventKind kind; int actorId=-1, targetId=-1, team=0; double value=0; };
struct WeaponConfig { double damage=10, range=700, fireInterval=1, aimTime=.28, reloadTime=2, spreadRadians=.13; int magazine=10; };
struct MatchConfig { double battleSeconds=420, sprintSeconds=300, resultsSeconds=30, baseHp=2500, npcHp=200, npcRespawnSeconds=12, orbRespawnSeconds=8; int naturalOrbs=120, npcCount=24; int64_t orbValue=2, npcReward=25; bool autoCombat=true, autoCollect=true; };
struct Fighter {
    int id=0, team=0; double hp=300, maxHp=300; int64_t score=0; int kills=0, deaths=0;
    // Fairy-wand base HP belongs only to this life; death and round reset clear it.
    double fairyWandHpBonus=0;
    double baseDamage=0,damageTaken=0; bool alive=true; double respawnRemaining=0, aimAngle=0;
    int ammo=10; double reloadRemaining=0, shotRemaining=0, aimRemaining=0, shapeCooldown=0, contactCooldown=0, acquisitionRemaining=0;
    int targetKind=0, targetIndex=-1;
    bool isHost=false,bossBuffEligible=false,heroBuff=false;
    double heroSwordRemaining=5,sniperAimDuration=0,heroActivatedAt=-1,evolutionStartedAt=-1;
    int heroBurstIndex=6;
    double heroBurstRemaining=0,heroBurstAngle=0;
    WeaponKind weaponKind=WeaponKind::Pistol;
    uint8_t unlockedWeapons=weaponBit(WeaponKind::Pistol);
    // Crates occupy only the right hand; permanent weaponKind and its saved
    // states are always the left hand. The old fallback field stays for callers.
    WeaponKind temporaryWeaponKind=WeaponKind::Pistol,weaponBeforeTemporary=WeaponKind::Pistol;
    double temporaryWeaponRemaining=0;
    WeaponRuntime rightWeapon{};
    // Equipped state lives in ammo/reloadRemaining/shotRemaining above. A switch
    // saves it here; holstered timers pause, so swapping cannot bypass them.
    std::array<WeaponState,WeaponCount> weaponStates{};
    double armor=0,maxArmor=0,evolutionRemaining=0,swordRemaining=5,hitFlash=0,healFlash=0,movementSlow=1,scoreFraction=0;
};
struct Base { Vec position; double hp=2500, maxHp=2500; bool alive=true; };
struct Orb { Vec position; int64_t value=2; bool natural=true, active=true; double respawnRemaining=0; };
struct Npc { Vec position; double hp=200, maxHp=200; bool active=true; double respawnRemaining=0,hitFlash=0; };
struct Shot { Vec from,to; int team=0; double life=.1; WeaponKind kind=WeaponKind::Pistol; double lifetime=.1; bool hit=false; bool rightHand=false; };
class Match {
public:
    World world;
    AudioEvents audio;
    MatchConfig config;
    WeaponConfig weapon;
    std::vector<Fighter> fighters;
    std::array<Base,3> bases;
    std::array<int64_t,3> teamScores{};
    // Registered participants, including dead players waiting for revival.
    std::array<int,3> teamCounts{};
    std::vector<int> leaderboard;
    std::vector<Orb> orbs;
    std::vector<Npc> npcs;
    std::vector<Shot> shots;
    std::vector<WeaponProjectile> projectiles;
    std::vector<WeaponExplosion> explosions;
    std::vector<WeaponCrate> weaponCrates;
    std::vector<Event> events;
    std::vector<DamageEvent> damageEvents;
    BossState boss;
    std::array<double,3> teamBuffRemaining{};
    std::vector<EvolutionPack> evolutionPacks;
    std::vector<SwordWave> swordWaves;
    double elapsed=0, intermissionRemaining=0;
    Phase phase=Phase::Battle;
    int round=1, mvpId=-1, fmvpId=-1, winnerTeam=0;
    int mostKillsId=-1,mostDamageTakenId=-1;
    static constexpr int ViewerCapacity=500;
    explicit Match(MatchConfig options={}) : config(options) { damageEvents.reserve(512); evolutionPacks.reserve(70);swordWaves.reserve(1024);weaponCrates.reserve(50);projectiles.reserve(WeaponProjectileCapacity);explosions.reserve(WeaponExplosionCapacity); initializeResources();resetBoss(); }
    WeaponConfig weaponFor(WeaponKind kind) const;
    WeaponConfig weaponFor(const Fighter& f) const;
    WeaponConfig weaponFor(const Fighter& f,bool rightHand) const;
    static double weaponMovementMultiplier(WeaponKind kind);
    bool collectWeaponCrate(int id,int index);
    bool damageEvolutionPack(int attackerId,int index,double amount);
    bool damageWeaponCrate(int attackerId,int index,double amount);
    bool addHost(int id,int team);
    bool setHostTeam(int id,int team);
    bool healLike(int id,int64_t count);
    bool grantShotgun(int id);
    bool grantWeapon(int id,WeaponKind kind);
    bool switchWeapon(int id,WeaponKind kind);
    bool collectEvolutionPack(int id,int index);
    bool grantEvolution(int id);
    bool damageEnvironment(int id,double amount);
    bool damageBoss(int attackerId,double amount);
    bool hasBossBuff(const Fighter& f) const {return f.alive&&f.bossBuffEligible&&f.team>=0&&f.team<3&&teamBuffRemaining[f.team]>0;}
    // Shape defence precedes this split. Armor absorbs up to 70% of damage,
    // spending 85% of the absorbed amount; any uncovered damage reaches HP.
    static double applyArmorDamage(double amount,double& hp,double& armor) {
        const double absorbed=std::min(std::max(0.0,amount)*.7,std::max(0.0,armor)/.85);
        armor=std::max(0.0,armor-absorbed*.85);
        const double hpDamage=amount-absorbed;
        const double applied=std::min(hp,std::max(0.0,hpDamage));hp=std::max(0.0,hp-applied);return applied;
    }
    static constexpr int TeamCapacity(int team) { return team==0?100:(team==1||team==2?200:0); }
    bool add(int id,Shape shape,int team=0) {
        if(team<0||team>2||teamCounts[team]>=TeamCapacity(team)||indices.count(id)||!world.add(id,shape,true)) return false;
        Fighter f; f.id=id; f.team=team; f.hp=f.maxHp=shapeHp(shape); resetWeaponAmmo(f);
        f.acquisitionRemaining=static_cast<double>(fighters.size()%30)/60;
        indices.emplace(id,static_cast<int>(fighters.size())); fighters.push_back(f); ++teamCounts[team];
        if(team==0) positionAtSpawn(static_cast<int>(fighters.size()-1));
        emit(EventKind::Joined,id,-1,team); if(phase!=Phase::Results) standingsDirty=true; return true;
    }
    bool chooseTeam(int id,int team) {
        auto* f=findFighter(id); if(!f||f->isHost||f->team!=0||team<1||team>2||teamCounts[team]>=TeamCapacity(team)||phase==Phase::Results) return false;
        --teamCounts[0]; ++teamCounts[team];
        f->team=team; f->score=0; f->targetKind=0; f->acquisitionRemaining=0;
        emit(EventKind::TeamChanged,id,-1,team); standingsDirty=true; return true;
    }
    bool changeShape(int id,Shape shape) {
        auto* f=findFighter(id); if(!f||f->isHost||phase==Phase::Results||f->shapeCooldown>1e-8) return false;
        const int i=indices.at(id); if(world.bodies[i].shape==shape) return true;
        const double fraction=f->maxHp>0?f->hp/f->maxHp:0;
        if(!world.reshape(id,shape)) return false;
        f->maxHp=maxHpFor(*f); f->hp=f->alive?f->maxHp*fraction:0; f->shapeCooldown=2;
        emit(EventKind::ShapeChanged,id,-1,f->team); return true;
    }
    // dt is fully consumed; phase transitions win ties at exact configured boundaries.
    void step(double dt) {
        if(!std::isfinite(dt)||dt<=0) return;
        while(dt>1e-8) {
            if(phase==Phase::Results) {
                const double chunk=std::min(dt,intermissionRemaining);
                intermissionRemaining=std::max(0.0,intermissionRemaining-chunk); dt-=chunk;
                if(intermissionRemaining<=1e-8) startNextRound();
                continue;
            }
            const double boundary=phase==Phase::Battle?config.sprintSeconds:config.battleSeconds;
            const double remaining=std::max(0.0,boundary-elapsed);
            const double chunk=std::min({dt,remaining,1.0/30.0});
            if(chunk>1e-8) { elapsed+=chunk; dt-=chunk; }
            // Discrete gameplay events occur at this step's endpoint. Phase cutoffs
            // win ties: no ordinary revive on Sprint entry, no gameplay in Results.
            if(elapsed+1e-8>=boundary) {
                elapsed=boundary;
                if(phase==Phase::Battle) enterSprint(); else finishRound();
            }
            if(chunk>1e-8&&phase!=Phase::Results) tick(chunk);
        }
    }
    void reset() {
        world.reset(); fighters.clear(); indices.clear(); round=1; elapsed=0; phase=Phase::Battle;
        intermissionRemaining=0; mvpId=fmvpId=mostKillsId=mostDamageTakenId=-1; winnerTeam=0; teamScores={}; leaderboard.clear();
        audio.clear(); events.clear(); shots.clear(); projectiles.clear(); explosions.clear(); damageEvents.clear(); damageCursor=0; teamCounts={}; seed=0xABCD1234u; initializeResources(); standingsDirty=true; standingsTimer=0;
        evolutionPacks.clear();swordWaves.clear();evolutionMinute=0;resetBoss();
        weaponCrates.clear();weaponCrateWave=0;
    }
    void startNextRound() {
        struct Identity {int id,team;Shape shape;bool host;uint8_t unlocked;WeaponKind equipped;}; std::vector<Identity> identities; identities.reserve(fighters.size());
        for(size_t i=0;i<fighters.size();++i) {endTemporaryWeapon(fighters[i]);identities.push_back({fighters[i].id,fighters[i].team,world.bodies[i].shape,fighters[i].isHost,fighters[i].unlockedWeapons,fighters[i].weaponKind});}
        const int next=round+1; reset(); round=next;
        for(const auto& identity:identities) {if(identity.host)addHost(identity.id,identity.team);else add(identity.id,identity.shape,identity.team);auto* f=findFighter(identity.id);if(f){f->unlockedWeapons=identity.unlocked;f->weaponKind=identity.equipped;resetWeaponAmmo(*f);}}
        events.clear(); emit(EventKind::RoundStarted,-1,-1,0,round); refreshStandings();
    }
    bool rebuildBase(int team) {
        if(team<1||team>2||phase!=Phase::Battle||elapsed+1e-8>=config.sprintSeconds||bases[team].alive) return false;
        bases[team].alive=true; bases[team].hp=bases[team].maxHp;
        emit(EventKind::BaseRebuilt,-1,-1,team); return true;
    }
    bool rebuildOrFortifyBase(int team) {return rebuildOrFortifyBase(team,1);}
    bool rebuildOrFortifyBase(int team,int64_t count) {
        if(count<=0||team<1||team>2||phase!=Phase::Battle||elapsed+1e-8>=config.sprintSeconds)return false;
        // The first unit rebuilds a destroyed base; only remaining units fortify.
        if(!bases[team].alive){rebuildBase(team);--count;}
        if(count>0){const double Gain=1000.0*static_cast<double>(count);bases[team].hp+=Gain;bases[team].maxHp+=Gain;emit(EventKind::BaseRebuilt,-1,-1,team,Gain);}
        return true;
    }
    bool revive(int id,bool gift=false) {
        auto* f=findFighter(id); if(!f||f->alive||phase==Phase::Results||(phase==Phase::Sprint&&!f->isHost&&!gift)) return false;
        resurrect(indices.at(id)); world.rebuildSpatial(); return true;
    }
    bool applyFairyWand(int id,int64_t count) {
        auto* f=findFighter(id);if(!f||count<=0||phase==Phase::Results)return false;
        // A dead recipient spends exactly one unit on the existing gift revival.
        if(!f->alive){if(!revive(id,true))return false;--count;f=findFighter(id);}
        if(count>0){
            const double oldMaxHp=f->maxHp;
            const double oldHp=f->hp;
            f->fairyWandHpBonus+=30.0*static_cast<double>(count);
            f->maxHp=maxHpFor(*f);
            // Preserve missing HP, including the doubled gain during evolution.
            f->hp=std::min(f->maxHp,f->hp+(f->maxHp-oldMaxHp));
            if(f->hp>oldHp)f->healFlash=.7;
        }
        return true;
    }
    Fighter* findFighter(int id) { const auto it=indices.find(id); return it==indices.end()?nullptr:&fighters[it->second]; }
    const Fighter* findFighter(int id) const { const auto it=indices.find(id); return it==indices.end()?nullptr:&fighters[it->second]; }
    // amount is base pistol damage. Shape damage/defence is applied here. Contact is a flat 15 before defence.
    bool damagePlayer(int attackerId,int victimId,double amount,bool contact=false) {
        auto* a=findFighter(attackerId); auto* v=findFighter(victimId);
        if(phase==Phase::Results||!a||!v||a==v||!a->alive||!v->alive||!validAmount(amount)||!hostile(*a,*v)) return false;
        const Shape attackShape=world.bodies[indices.at(attackerId)].shape;
        const Shape victimShape=world.bodies[indices.at(victimId)].shape;
        if(contact) {
            if(attackShape!=Shape::Triangle||a->contactCooldown>1e-8) return false;
            amount=15*(hasBossBuff(*a)?1.2:1); a->contactCooldown=.75;
        } else amount*=damageMultiplierFor(*a,v->team==0);
        if(victimShape==Shape::Circle&&!v->isHost) amount*=.5;
        const double applied=applyFighterDamage(*v,amount);v->hitFlash=.16;
        audio.emit(AudioKind::FighterHit,victimId);
        emitDamage(world.bodies[indices.at(victimId)].position,applied,v->team,1,victimId);
        if(v->hp<=1e-8) kill(indices.at(attackerId),indices.at(victimId));
        return true;
    }
    bool damageNpc(int attackerId,int index,double amount) {
        auto* a=findFighter(attackerId);
        if(phase==Phase::Results||!a||!a->alive||index<0||index>=static_cast<int>(npcs.size())||!npcs[index].active||!validAmount(amount)) return false;
        auto& n=npcs[index]; const Shape shape=world.bodies[indices.at(attackerId)].shape;
        const double applied=std::min(n.hp,amount*damageMultiplierFor(*a,true));
        n.hp=std::max(0.0,n.hp-applied);
        n.hitFlash=.16;audio.emit(AudioKind::NpcHit,attackerId);
        emitDamage(n.position,applied,0,2,index);
        if(n.hp<=1e-8) {
            n.active=false; audio.emit(AudioKind::NpcDeath,attackerId); n.respawnRemaining=config.npcRespawnSeconds;
            const int64_t reward=a->team==0||a->isHost?0:awardScore(*a,config.npcReward*(shape==Shape::Square?2:1));
            emit(EventKind::NpcKilled,attackerId,index,a->team,static_cast<double>(reward)); standingsDirty=true;
        }
        return true;
    }
    bool damageBase(int attackerId,int team,double amount) {
        auto* a=findFighter(attackerId);
        if(phase==Phase::Results||!a||!a->alive||team<1||team>2||a->team==team||!bases[team].alive||!validAmount(amount)) return false;
        const double applied=std::min(bases[team].hp,amount*damageMultiplierFor(*a,false));
        bases[team].hp-=applied; a->baseDamage+=applied;
        emitDamage(bases[team].position,applied,team,3,team);
        if(bases[team].hp<=1e-8) { bases[team].hp=0; bases[team].alive=false; emit(EventKind::BaseDestroyed,attackerId,-1,team); }
        return true;
    }
    bool collectOrb(int id,int index) {
        auto* f=findFighter(id);
        if(phase==Phase::Results||!f||!f->alive||f->team==0||f->isHost||index<0||index>=static_cast<int>(orbs.size())||!orbs[index].active) return false;
        auto& orb=orbs[index]; const bool doubled=orb.natural&&world.bodies[indices.at(id)].shape==Shape::Square;
        const int64_t value=orb.value*(doubled?2:1);const int64_t reward=orb.natural?awardScore(*f,value):value;if(!orb.natural)f->score+=reward; orb.active=false;
        orb.respawnRemaining=orb.natural?config.orbRespawnSeconds:0;
        audio.emit(AudioKind::Orb,id); emit(EventKind::OrbCollected,id,index,f->team,static_cast<double>(reward)); standingsDirty=true; return true;
    }
    void refreshStandings() {
        if(phase==Phase::Results) return;
        teamScores={}; leaderboard.clear();
        for(size_t i=0;i<fighters.size();++i) if(fighters[i].team!=0&&!fighters[i].isHost) { teamScores[fighters[i].team]+=fighters[i].score; leaderboard.push_back(static_cast<int>(i)); }
        const size_t count=std::min<size_t>(20,leaderboard.size());
        std::partial_sort(leaderboard.begin(),leaderboard.begin()+count,leaderboard.end(),[this](int a,int b) {return betterScore(a,b);});
        leaderboard.resize(count); standingsDirty=false;
    }
private:
    static constexpr int ResourceCells=32;
    std::array<std::vector<int>,ResourceCells*ResourceCells> orbGrid,npcGrid;
    std::unordered_map<int,int> indices;
    std::vector<int> candidates;
    uint32_t seed=0xABCD1234u;
    size_t shotCursor=0;
    size_t damageCursor=0;
    bool standingsDirty=true;
    double standingsTimer=0;
    int evolutionMinute=0;
    int weaponCrateWave=0;
    static bool validAmount(double v) { return std::isfinite(v)&&v>0; }
    static double applyFighterDamage(Fighter& victim,double amount) {
        const double armorBefore=victim.armor;
        const double hpDamage=applyArmorDamage(amount,victim.hp,victim.armor);
        // Count actual HP lost plus damage absorbed, not the armor's 85% cost.
        // Mitigation already happened; overkill beyond remaining HP is excluded.
        victim.damageTaken+=hpDamage+std::max(0.0,armorBefore-victim.armor)/.85;
        return hpDamage;
    }
    static double shapeHp(Shape shape) { return shape==Shape::Square?250:(shape==Shape::Rectangle?200:300); }
    double maxHpFor(const Fighter& f) const {
        if(f.heroBuff)return 2100+f.fairyWandHpBonus;
        const double base=f.isHost?3000:shapeHp(world.bodies[indices.at(f.id)].shape);
        return (base+f.fairyWandHpBonus)*(f.evolutionRemaining>0?2:1);
    }
    static double damageMultiplier(Shape shape,bool neutral) { return shape==Shape::Triangle?1.5:(shape==Shape::Square&&neutral?2.5:1); }
    double damageMultiplierFor(const Fighter& f,bool neutral) const {return (f.isHost?1:damageMultiplier(world.bodies[indices.at(f.id)].shape,neutral))*(hasBossBuff(f)?1.2:1);}
    double scoreMultiplierFor(const Fighter& f) const {return hasBossBuff(f)?1.2:1;}
    int64_t awardScore(Fighter& f,int64_t value) {const double total=value*scoreMultiplierFor(f)+f.scoreFraction;const int64_t whole=static_cast<int64_t>(std::floor(total+1e-9));f.scoreFraction=std::max(0.0,total-whole);f.score+=whole;return whole;}
    static bool hostile(const Fighter& a,const Fighter& b) { return a.team==0||b.team==0||a.team!=b.team; }
    double random() { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return static_cast<double>(seed)/4294967295.0; }
    void emit(EventKind kind,int actor=-1,int target=-1,int team=0,double value=0) {
        if(events.size()>=512) events.erase(events.begin()); events.push_back({kind,actor,target,team,value});
    }
    void emitDamage(Vec position,double amount,int team,int kind,int id,NumberKind numberKind=NumberKind::Damage) {
        if(amount<=0)return;
        const DamageEvent event{position,amount,team,kind,id,numberKind};
        if(damageEvents.size()<512){damageEvents.push_back(event);damageCursor=0;}
        else {damageEvents[damageCursor]=event;damageCursor=(damageCursor+1)%512;}
    }
    static int cell(double position) { return std::clamp(static_cast<int>(position/(World::Size/ResourceCells)),0,ResourceCells-1); }
    static int cellIndex(Vec p) { return cell(p.y)*ResourceCells+cell(p.x); }
    void initializeResources() {
        bases[0]={{World::Size*.5,World::Size*.5},0,0,false};
        bases[1]={{World::Size*.18,World::Size*.5},config.baseHp,config.baseHp,true};
        bases[2]={{World::Size*.82,World::Size*.5},config.baseHp,config.baseHp,true};
        orbs.clear(); npcs.clear(); for(auto& bucket:orbGrid) bucket.clear(); for(auto& bucket:npcGrid) bucket.clear();
        for(int i=0;i<std::max(0,config.naturalOrbs);++i) {
            const Vec p={80+random()*(World::Size-160),80+random()*(World::Size-160)};
            orbGrid[cellIndex(p)].push_back(i); orbs.push_back({p,config.orbValue,true,true,0});
        }
        for(int i=0;i<std::max(0,config.npcCount);++i) {
            const Vec p={120+random()*(World::Size-240),120+random()*(World::Size-240)};
            npcGrid[cellIndex(p)].push_back(i); npcs.push_back({p,config.npcHp,config.npcHp,true,0});
        }
    }
    bool betterScore(int a,int b) const {
        const auto& x=fighters[a]; const auto& y=fighters[b];
        if(x.score!=y.score) return x.score>y.score;
        if(x.kills!=y.kills) return x.kills>y.kills;
        return x.id<y.id;
    }
    void enterSprint() {
        phase=Phase::Sprint;
        refreshStandings();
        // Take a snapshot before revival; score ties follow normal ranking rules.
        const auto heroes=leaderboard;
        for(size_t rank=0;rank<std::min<size_t>(10,heroes.size());++rank) {
            const int index=heroes[rank]; if(!fighters[index].alive)resurrect(index);
            auto& hero=fighters[index];hero.heroBuff=true;hero.hp=hero.maxHp=maxHpFor(hero);
            hero.armor=hero.maxArmor=300;hero.heroSwordRemaining=5;hero.heroActivatedAt=elapsed;hero.heroBurstIndex=6;hero.heroBurstRemaining=0;
        }
        world.rebuildSpatial();
        for(int t=1;t<=2;++t) { if(bases[t].alive) emit(EventKind::BaseDestroyed,-1,-1,t); bases[t].alive=false; bases[t].hp=0; }
        emit(EventKind::SprintStarted);
    }
    void finishRound() {
        refreshStandings(); std::array<int,3> kills{};
        for(const auto& f:fighters) if(!f.isHost)kills[f.team]+=f.kills;
        if(teamScores[1]!=teamScores[2]) winnerTeam=teamScores[1]>teamScores[2]?1:2;
        else if(kills[1]!=kills[2]) winnerTeam=kills[1]>kills[2]?1:2;
        else winnerTeam=round%2?1:2;
        mvpId=fmvpId=-1;
        if(winnerTeam==1||winnerTeam==2) {
            std::array<int,3> bestIndex{{-1,-1,-1}};
            for(size_t i=0;i<fighters.size();++i) {
                const auto& f=fighters[i];
                if(f.isHost||f.team<1||f.team>2) continue;
                const int index=static_cast<int>(i);
                if(bestIndex[f.team]<0||betterScore(index,bestIndex[f.team])) bestIndex[f.team]=index;
            }
            if(bestIndex[winnerTeam]>=0) mvpId=fighters[bestIndex[winnerTeam]].id;
            const int losingTeam=3-winnerTeam;
            if(bestIndex[losingTeam]>=0) fmvpId=fighters[bestIndex[losingTeam]].id;
        }
        mostKillsId=mostDamageTakenId=-1;
        const auto bestHonor=[&](auto value) {
            const Fighter* best=nullptr;
            for(const auto& f:fighters) {
                if(f.isHost||value(f)<=0)continue;
                if(!best||value(f)>value(*best)||(value(f)==value(*best)&&(f.score>best->score||(f.score==best->score&&f.id<best->id))))best=&f;
            }
            return best?best->id:-1;
        };
        mostKillsId=bestHonor([](const Fighter& f){return f.kills;});
        mostDamageTakenId=bestHonor([](const Fighter& f){return f.damageTaken;});
        // Results freezes gameplay timers, so discard crate leases at the
        // battle cutoff instead of carrying them through the intermission.
        for(auto& f:fighters)endTemporaryWeapon(f);
        phase=Phase::Results; intermissionRemaining=config.resultsSeconds;
        shots.clear(); projectiles.clear(); explosions.clear(); swordWaves.clear(); emit(EventKind::RoundEnded,mvpId,fmvpId,winnerTeam);
    }
    void dropScore(Vec position,int64_t value) {
        if(value<=0) return;
        // Reuse consumed drops. Natural orb slots are stable because they respawn.
        for(size_t i=static_cast<size_t>(std::max(0,config.naturalOrbs));i<orbs.size();++i) if(!orbs[i].active) {
            auto& previous=orbGrid[cellIndex(orbs[i].position)]; previous.erase(std::remove(previous.begin(),previous.end(),static_cast<int>(i)),previous.end());
            orbs[i]={position,value,false,true,0}; orbGrid[cellIndex(position)].push_back(static_cast<int>(i)); return;
        }
        orbGrid[cellIndex(position)].push_back(static_cast<int>(orbs.size())); orbs.push_back({position,value,false,true,0});
    }
    void kill(int attacker,int victim) {
        audio.emit(AudioKind::FighterDeath,fighters[victim].id);
        auto& v=fighters[victim];Fighter* a=attacker>=0?&fighters[attacker]:nullptr;if(a)++a->kills; ++v.deaths;
        const int64_t carried=v.score/5; // Transfer the same whole-point20% in both ledgers.
        v.score-=carried;
        if(!a||a->team==0||a->isHost) dropScore(world.bodies[victim].position,carried); else a->score+=carried;
        endTemporaryWeapon(v);
        endEvolution(v);
        v.fairyWandHpBonus=0;v.maxHp=maxHpFor(v);
        v.hp=0; v.alive=false; v.bossBuffEligible=false; v.respawnRemaining=v.team==0?20:15; v.targetKind=0;
        v.heroBurstIndex=6;v.heroBurstRemaining=0;v.healFlash=0;
        world.bodies[victim].active=false; standingsDirty=true;
        emit(EventKind::Kill,a?a->id:-1,v.id,a?a->team:0,static_cast<double>(carried));
    }
    void resurrect(int index) {
        auto& f=fighters[index]; auto& b=world.bodies[index];
        f.alive=true; f.bossBuffEligible=false; f.hp=f.maxHp; f.respawnRemaining=0; resetWeaponAmmo(f);
        f.armor=f.maxArmor=f.heroBuff?300:(f.isHost?500:0);f.hitFlash=0;f.healFlash=0;f.movementSlow=1;f.heroSwordRemaining=5;f.heroBurstIndex=6;f.heroBurstRemaining=0;
        f.reloadRemaining=f.shotRemaining=f.aimRemaining=0; f.targetKind=0; f.acquisitionRemaining=0;
        b.active=true;
        positionAtSpawn(index);
        const double angle=random()*detail::Tau;
        b.speedScale=(f.isHost?.5:1)*weaponMovementMultiplier(f.weaponKind);b.velocity={std::cos(angle)*165*b.speedScale,std::sin(angle)*165*b.speedScale};
        emit(EventKind::Respawn,f.id,-1,f.team);
    }
    void positionAtSpawn(int index) {
        const auto& f=fighters[index]; auto& b=world.bodies[index];
        const Vec center=f.team==0?Vec{World::Size*.5,World::Size*.24}:bases[f.team].position;
        const double angle=random()*6.283185307179586, radius=80+random()*280;
        b.position={std::clamp(center.x+std::cos(angle)*radius,50.0,World::Size-50),std::clamp(center.y+std::sin(angle)*radius,50.0,World::Size-50)};
        world.rebuildSpatial();
    }
    void tick(double dt) {
        for(auto& remaining:teamBuffRemaining){remaining=std::max(0.0,remaining-dt);if(remaining<=1e-8)remaining=0;}
        bool reactivated=false;
        for(size_t i=0;i<fighters.size();++i) {
            auto& f=fighters[i]; f.shapeCooldown=std::max(0.0,f.shapeCooldown-dt); f.contactCooldown=std::max(0.0,f.contactCooldown-dt);f.hitFlash=std::max(0.0,f.hitFlash-dt);f.healFlash=std::max(0.0,f.healFlash-dt);
            if(!f.alive) {
                f.respawnRemaining=std::max(0.0,f.respawnRemaining-dt);
                if(f.respawnRemaining<=1e-8&&(f.isHost||(phase==Phase::Battle&&(f.team==0||bases[f.team].alive)))) { resurrect(static_cast<int>(i)); reactivated=true; }
            }
        }
        if(reactivated) world.rebuildSpatial();
        tickBoss(dt);
        tickWeaponCrates(dt);
        for(size_t i=0;i<fighters.size();++i){const double scale=(fighters[i].isHost?.5:1)*fighters[i].movementSlow*weaponMovementMultiplier(fighters[i].weaponKind);auto& b=world.bodies[i];if(b.speedScale>0)b.velocity=b.velocity*(scale/b.speedScale);b.speedScale=scale;}
        tickEvolution(dt);
        for(auto& orb:orbs) if(!orb.active&&orb.natural) {
            orb.respawnRemaining=std::max(0.0,orb.respawnRemaining-dt); if(orb.respawnRemaining<=1e-8) orb.active=true;
        }
        for(auto& npc:npcs) { npc.hitFlash=std::max(0.0,npc.hitFlash-dt); if(!npc.active) {
            npc.respawnRemaining=std::max(0.0,npc.respawnRemaining-dt);
            if(npc.respawnRemaining<=1e-8) {npc.active=true;npc.hp=npc.maxHp;}
        }}
        for(auto& shot:shots) shot.life-=dt;
        shots.erase(std::remove_if(shots.begin(),shots.end(),[](const Shot& s){return s.life<=0;}),shots.end());
        applyBossAttraction(dt);
        syncArenaObstacles();
        world.step(dt);
        if(config.autoCollect) attractOrbs(dt);
        tickProjectiles(dt);
        if(config.autoCombat) for(const auto& contact:world.contacts) {
            const int a=contact.first,b=contact.second;
            if(!fighters[a].alive||!fighters[b].alive||!hostile(fighters[a],fighters[b])) continue;
            if(world.bodies[a].shape==Shape::Triangle&&fighters[a].contactCooldown<=1e-8) damagePlayer(fighters[a].id,fighters[b].id,15,true);
            if(world.bodies[b].shape==Shape::Triangle&&fighters[b].contactCooldown<=1e-8) damagePlayer(fighters[b].id,fighters[a].id,15,true);
        }
        for(size_t i=0;i<fighters.size();++i) if(fighters[i].alive) {
            if(config.autoCombat) tickWeapon(static_cast<int>(i),dt);
            if(config.autoCollect&&fighters[i].team!=0&&!fighters[i].isHost) collectNearby(static_cast<int>(i));
        }
        standingsTimer-=dt;
        if(standingsDirty&&standingsTimer<=0) {refreshStandings();standingsTimer=.25;}
    }
    static WeaponRuntimeView weaponRuntime(Fighter& f,bool rightHand);
    void acquireTarget(int index,bool rightHand=false);
    bool targetPosition(int index,Vec& position,bool rightHand=false) const;
    void tickWeapon(int index,double dt);
    void tickWeaponHand(int index,double dt,bool rightHand);
    void resetWeaponAmmo(Fighter& f);
    void launchProjectile(int index,Vec direction,WeaponKind kind,double damage,bool rightHand);
    void tickProjectiles(double dt);
    void explodeRocket(const WeaponProjectile& projectile);
    void collectNearby(int index);
    void resetBoss();
    void tickBoss(double dt);
    void applyBossAttraction(double dt);
    void tickWeaponCrates(double dt);
    void endTemporaryWeapon(Fighter& f);
    void tickEvolution(double dt);
    void endEvolution(Fighter& f);
    void emitSwordPulse(int index);
    void emitHeroSwordPulse(int index,int ray,double angle,double birthDelay);
    void attractOrbs(double dt);
    void tickSwordWaves(double dt);
    void syncArenaObstacles();
};

inline void Match::collectNearby(int index) {
    const Vec p=world.bodies[index].position;
    for(int y=cell(p.y-46);y<=cell(p.y+46);++y) for(int x=cell(p.x-46);x<=cell(p.x+46);++x)
        for(int candidate:orbGrid[y*ResourceCells+x]) {
            const auto& orb=orbs[candidate]; const Vec delta=orb.position-p;
            if(orb.active&&delta.dot(delta)<=46*46) collectOrb(fighters[index].id,candidate);
        }
}
#include "ArenaCombat.inl"
#include "ArenaEvolution.inl"
}
#include "ArenaBoss.inl"
#include "ArenaWeapons.inl"
#include "ArenaPickups.inl"
#include "ArenaObstacles.inl"

