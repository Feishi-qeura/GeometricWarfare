#include "../Source/GeometricWarfare/Simulation/ArenaMatch.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace gw;
static int assertions=0;
static void require(bool value,const char* message){++assertions;if(!value)throw std::runtime_error(message);}
static Match quiet(){MatchConfig c;c.autoCombat=false;c.autoCollect=false;c.naturalOrbs=0;c.npcCount=0;return Match(c);}
static void place(Match& m,int id,Vec p,Vec velocity={}){auto* b=m.world.find(id);b->position=p;b->velocity=velocity;b->angle=b->spin=0;m.world.rebuildSpatial();}
static bool touching(const Body& b,const StaticObstacle& obstacle){Body proxy;proxy.position=obstacle.position;Vec normal;double depth;return detail::overlap(b,detail::geometry(b),proxy,obstacle.geometry,normal,depth);}
static void bossCollision(){
    for(int kind=0;kind<5;++kind){auto m=quiet();if(kind<4)m.add(1,static_cast<Shape>(kind),1);else m.addHost(1,1);
        m.boss.active=m.boss.spawned=true;place(m,1,{3740,4000},{280,0});bool bounced=false;
        for(int step=0;step<160;++step){m.step(1.0/120);const auto& body=*m.world.find(1);
            bounced|=body.velocity.x<0;require(m.world.obstacles.size()==1&&!touching(body,m.world.obstacles[0]),"each moving shape and host stays outside the fixed boss collision polygon");}
        require(bounced,"boss face reflects approaching player velocity");
        require(m.world.bodies.size()==1&&m.fighters.size()==1&&m.world.contacts.empty(),"boss does not consume a viewer slot or enter player contact damage pairs");
        require((m.boss.position-Vec{4000,4000}).length()<1e-8,"player collisions cannot move the boss from its fixed center");
        require(m.damageBoss(1,100000),"collision fixture kills boss");place(m,1,{4000,4000},{100,0});m.step(1.0/120);
        require(m.world.obstacles.empty()&&m.world.find(1)->position.x<4002,"dead boss stops blocking players immediately on the next physics tick");
    }
}
static void resources(){
    for(int kind=0;kind<3;++kind){auto m=quiet();m.add(7,Shape::Square,1);
        if(kind==0)m.npcs.push_back({{600,600},200,200,true,0});
        else if(kind==1)m.evolutionPacks.push_back({{600,600},true,0});
        else m.weaponCrates.push_back({{600,600},WeaponKind::Sniper,true,0});
        place(m,7,{500,600},{280,0});bool bounced=false;
        for(int step=0;step<60;++step){m.step(1.0/120);bounced|=m.world.find(7)->velocity.x<0;
            require(m.world.obstacles.size()==1&&!touching(*m.world.find(7),m.world.obstacles[0]),"active NPC and both reward containers block player geometry");}
        require(bounced,"neutral actor reflects velocity instead of letting player pass through");
        if(kind==0){m.npcs[0].active=false;m.npcs[0].respawnRemaining=100;}
        else if(kind==1)m.evolutionPacks[0].active=false;else m.weaponCrates[0].active=false;
        place(m,7,{590,600},{200,0});m.step(.1);
        require(m.world.obstacles.empty()&&m.world.find(7)->position.x>600,"inactive or claimed container and defeated NPC remove their obstacle");
    }
    auto m=quiet();m.add(4,Shape::Circle,1);m.npcs.push_back({{600,600},0,200,false,.02});place(m,4,{600,600});
    m.step(.01);require(m.world.obstacles.empty(),"dead NPC does not collide during respawn delay");m.step(.02);
    require(m.world.obstacles.size()==1&&!touching(*m.world.find(4),m.world.obstacles[0]),"revived NPC separates an occupying player without adding a fighter");
}
static void bossImmunity(){
    for(BossAttack attack:{BossAttack::Bullet,BossAttack::LaserActive,BossAttack::StompWave}){
        auto m=quiet();m.config.autoCombat=true;m.weapon.range=0;m.boss.active=m.boss.spawned=true;m.boss.hp=6400;
        m.npcs.push_back({{4400,4000},200,200,true,0});m.evolutionPacks.push_back({{4450,4000},true,0});
        m.weaponCrates.push_back({{4500,4000},WeaponKind::Sniper,true,0});m.add(3,Shape::Rectangle,1);place(m,3,{4550,4000});m.findFighter(3)->hp=100000;
        m.boss.attack=attack;m.boss.attackInitialized=false;m.boss.laserDirection={1,0};
        if(attack==BossAttack::Bullet)m.boss.projectiles[0]={{4000,4000},{1000,0},BossBulletRadius,0,200,true};
        m.step(1);
        require(m.npcs[0].active&&m.npcs[0].hp==200&&(m.npcs[0].position-Vec{4400,4000}).length()<1e-8,"boss missile, laser/scorch and shockwave cannot damage or displace neutral NPC");
        require(m.evolutionPacks[0].active&&m.weaponCrates[0].active&&m.evolutionPacks[0].hp==500&&m.weaponCrates[0].hp==500,"both reward containers remain immune to every boss attack");
    }
}
int main(){int failures=0;const auto run=[&](const char* name,auto test){try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};
    run("boss collision",bossCollision);run("resource collision lifecycle",resources);run("boss neutral immunity",bossImmunity);
    std::cout<<"Obstacle tests: "<<assertions<<" assertions, "<<failures<<" failures\n";return failures?1:0;
}
