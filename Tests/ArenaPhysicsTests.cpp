#include "../Source/GeometricWarfare/Simulation/ArenaPhysics.h"
#include <iostream>
#include <set>
#include <limits>

static int failures = 0;
static void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
static bool close(gw::Vec a, gw::Vec b) { return (a-b).length()<0.0001; }

int main() {
    gw::World w;
    int spawned=0;
    for(int i=0;i<5000;++i) if(w.add(i,static_cast<gw::Shape>(i%4))) ++spawned;
    check(spawned==5000, "all 5000 players can spawn");
    if(spawned!=5000) return 1;
    check(!w.add(5001,gw::Shape::Circle), "capacity is enforced after 5000 players");
    bool nonoverlap=true;
    std::vector<int> nearby;
    for(size_t i=0;i<w.bodies.size();++i) {
        w.query(w.bodies[i].position,72,nearby);
        for(int j:nearby) if(j!=static_cast<int>(i)) {
            gw::Vec n; double d;
            nonoverlap=nonoverlap && !gw::overlap(w.bodies[i],w.bodies[j],n,d);
        }
    }
    check(nonoverlap, "5000-player spawn leaves physical separation");
    check(w.find(4999)==&w.bodies[4999], "lookup resolves final player to stable index");
    check(!w.add(1,gw::Shape::Square), "duplicate id cannot replace an existing player");
    check(!w.find(7000), "missing player lookup returns null");
    const gw::World& cw=w;
    check(cw.find(2500)==&w.bodies[2500], "const lookup resolves player");
    for(int i=0;i<240;++i) w.step(1.0/120);
    bool bounded=true;
    for(const auto& b:w.bodies) {
        bounded=bounded && std::isfinite(b.position.x) && std::isfinite(b.position.y)
            && std::isfinite(b.velocity.x) && std::isfinite(b.velocity.y) && b.velocity.length()<=280.001;
        for(auto p:gw::vertices(b)) bounded=bounded && p.x>=-0.01 && p.y>=-0.01 && p.x<=8000.01 && p.y<=8000.01;
    }
    check(bounded, "5000 moving players remain finite and inside the enlarged arena");
    for(gw::Vec center: {gw::Vec{0,0},gw::Vec{4000,4000},gw::Vec{7900,7900}}) {
        w.query(center,975,nearby);
        std::set<int> actual(nearby.begin(),nearby.end()), expected;
        for(size_t i=0;i<w.bodies.size();++i) {
            const gw::Vec d=w.bodies[i].position-center;
            if(w.bodies[i].active && d.x*d.x+d.y*d.y<=975.0*975.0) expected.insert(static_cast<int>(i));
        }
        check(actual==expected && actual.size()==nearby.size(), "spatial query matches full scan after simulation");
    }
    w.reset();
    check(w.bodies.empty() && w.contacts.empty() && !w.find(2500), "reset clears bodies, contacts and id lookup");
    w.query({4000,4000},8000,nearby);
    check(nearby.empty(), "reset clears spatial query results");
    check(!w.add(-1,gw::Shape::Circle), "negative viewer id rejected");
    check(!w.add(1,static_cast<gw::Shape>(100)), "invalid shape rejected");
    check(w.add(1,gw::Shape::Circle), "valid spawn works after rejected inputs");
    w.bodies[0].position={25,500}; w.bodies[0].velocity={-180,0};
    w.step(0.1);
    check(w.bodies[0].velocity.x>0 && w.bodies[0].position.x>=22, "left wall reflects velocity and contains body");

    w.reset(); w.add(101,gw::Shape::Square); w.add(202,gw::Shape::Square);
    w.bodies[0].position={460,500}; w.bodies[1].position={540,500};
    w.bodies[0].velocity={180,0}; w.bodies[1].velocity={-180,0};
    w.bodies[0].angle=w.bodies[1].angle=0;
    bool touched=false;
    for(int i=0;i<60;++i) {
        w.step(1.0/120);
        for(const auto pair:w.contacts) {
            check(pair.first==0 && pair.second==1, "contacts use stable indices, not external ids");
            touched=true;
        }
        check(w.contacts.size()<=1, "contact pairs are unique within a step");
    }
    check(touched && w.bodies[0].velocity.x<0 && w.bodies[1].velocity.x>0, "head-on collision reports contact and rebounds");
    check(w.contacts.empty(), "contacts expire after separated step");
    w.bodies[0].position={200,200}; w.bodies[1].position={200,200};
    w.bodies[1].active=false;
    const auto deadPosition=w.bodies[1].position;
    w.rebuildSpatial();
    w.query({200,200},1,nearby);
    check(nearby.size()==1 && nearby[0]==0, "inactive overlapping players are excluded from queries");
    w.step(1.0/120);
    check(close(w.bodies[1].position,deadPosition) && w.contacts.empty(), "inactive players do not integrate or collide");
    check(w.reshape(202,gw::Shape::Triangle) && !w.bodies[1].active, "shape changes preserve inactive state");
    w.bodies[1].active=true; w.bodies[1].position={7000,7000};
    w.rebuildSpatial();
    w.query({7000,7000},0,nearby);
    check(nearby.size()==1 && nearby[0]==1, "rebuild indexes teleported and revived players");
    check(w.find(202)==&w.bodies[1], "death and revival retain body index");
    w.query({7000,7000},-1,nearby); check(nearby.empty(), "negative query radius gives no matches");
    w.query({NAN,0},100,nearby); check(nearby.empty(), "invalid query center gives no matches");

    gw::Body rect,circle; rect.shape=gw::Shape::Rectangle; rect.position={100,100};
    circle.position={100,140}; gw::Vec normal; double depth=0;
    check(!gw::overlap(rect,circle,normal,depth), "rectangle narrow side remains distinct from a circle");
    circle.position={140,100};
    check(gw::overlap(rect,circle,normal,depth) && normal.x>0.99 && depth>11.9 && depth<12.1, "circle and rectangle have meaningful contact depth");
    gw::Body triangle; triangle.shape=gw::Shape::Triangle; triangle.position={100,100};
    circle.position={130,75};
    check(!gw::overlap(triangle,circle,normal,depth), "triangle corners use convex geometry");

    for(int shape=0;shape<4;++shape)for(double scale:{1.0,2.5}) {
        gw::World solid;solid.add(7,static_cast<gw::Shape>(shape));
        auto& player=solid.bodies[0];player.position={500,600};player.velocity={280,0};player.spin=0;player.scale=scale;
        solid.obstacles.push_back(gw::StaticObstacle::box({600,600},18));solid.rebuildObstacleSpatial();
        for(int step=0;step<120;++step)solid.step(1.0/120);
        check(player.velocity.x<0&&player.position.x<560, "each shape and host size rebounds from a stationary box without crossing it");
        check(solid.bodies.size()==1&&solid.contacts.empty()&&solid.find(7)==&player, "obstacle collisions preserve viewer indices and never create fighter contacts");
        check(close(solid.obstacles[0].position,{600,600}), "static obstacle remains fixed under player impact");
    }
    gw::World solid;solid.add(8,gw::Shape::Circle);auto& player=solid.bodies[0];
    solid.obstacles.push_back(gw::StaticObstacle::hexagon({600,600},32));solid.rebuildObstacleSpatial();
    player.position={600,600};player.velocity={90,0};player.spin=0;solid.step(1.0/120);
    gw::Body proxy;proxy.position=solid.obstacles[0].position;
    check(!gw::detail::overlap(player,gw::detail::geometry(player),proxy,solid.obstacles[0].geometry,normal,depth), "a player initially inside a spawning convex obstacle is separated immediately");
    solid.obstacles[0].active=false;player.position={540,600};player.velocity={280,0};solid.step(.25);
    check(player.position.x>600&&player.velocity.x>0, "deactivated obstacles stop colliding even before spatial rebuild");
    solid.reset();check(solid.obstacles.empty(), "world reset clears static scenery and its spatial index");

    w.reset();
    for(int i=0;i<5000;++i) w.add(i,static_cast<gw::Shape>(i%4));
    for(auto& body:w.bodies) body.position={4000,4000};
    w.bodies[0].active=false;
    w.rebuildSpatial();
    w.queryLimited({4000,4000},100,nearby,64);
    check(nearby.size()==64 && std::find(nearby.begin(),nearby.end(),0)==nearby.end(), "bounded query caps results and excludes inactive players");
    w.queryLimited({4000,4000},100,nearby,0);
    check(nearby.empty(), "zero bounded query budget produces no results");
    w.query({4000,4000},100,nearby);
    check(nearby.size()==4999, "exact query remains complete in a dense crowd");
    w.queryLimitedFiltered({4000,4000},100,nearby,[](int index){return index==1;},128);
    check(nearby.size()==1&&nearby[0]==1, "filtered targeting finds an enemy hidden behind thousands of rejected allies");
    w.queryLimitedFiltered({4000,4000},100,nearby,[](int index){return index%2==0;},7);
    check(nearby.size()==7&&std::all_of(nearby.begin(),nearby.end(),[](int index){return index!=0&&index%2==0;}), "filtered targeting limits matching active results rather than scanned bodies");
    w.bodies[0].active=true;
    for(int i=0;i<12;++i) w.step(1.0/120);
    bool finite=true;
    for(const auto& body:w.bodies) finite=finite && std::isfinite(body.position.x) && std::isfinite(body.position.y);
    check(finite && w.contacts.size()<=80000, "fully overlapping crowd stays finite with bounded contact work");

    gw::World a,b; a.add(1,gw::Shape::Triangle); b.add(1,gw::Shape::Triangle);
    for(int i=0;i<60;++i) a.step(1.0/60);
    for(int i=0;i<120;++i) b.step(1.0/120);
    check(close(a.bodies[0].position,b.bodies[0].position), "fixed stepping is frame-rate independent");
    auto before=a.bodies[0].position;
    a.step(-1); a.step(NAN);
    check(close(a.bodies[0].position,before), "invalid delta ignored");
    std::cout << "Physics tests: " << failures << " failures\n";
    return failures ? 1 : 0;
}
