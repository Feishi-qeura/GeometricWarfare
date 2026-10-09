#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gw {
struct Vec {
    double x=0, y=0;
    Vec operator+(Vec b) const { return {x+b.x,y+b.y}; }
    Vec operator-(Vec b) const { return {x-b.x,y-b.y}; }
    Vec operator*(double s) const { return {x*s,y*s}; }
    Vec& operator+=(Vec b) { x+=b.x; y+=b.y; return *this; }
    double dot(Vec b) const { return x*b.x+y*b.y; }
    double length() const { return std::sqrt(dot(*this)); }
};
enum class Shape { Circle, Square, Rectangle, Triangle };
struct Body {
    int id=0;
    Shape shape=Shape::Circle;
    Vec position, velocity;
    double angle=0, spin=0;
    bool active=true;
    double scale=1, speedScale=1;
};
namespace detail {
constexpr double Tau=6.283185307179586;
inline bool validShape(Shape shape) { return shape>=Shape::Circle && shape<=Shape::Triangle; }
// Rotated local geometry: translations during separation never invalidate it.
// Circles use analytic support; six vertices also cover neutral hexagons.
struct Geometry {
    std::array<Vec,6> points{}, axes{};
    int count=0, axisCount=0;
    double radius=22, minx=-22, miny=-22, maxx=22, maxy=22;
    Shape shape=Shape::Circle;
    double angle=0, scale=1;
};
inline Geometry geometry(const Body& b) {
    Geometry g; g.shape=b.shape; g.angle=b.angle; g.scale=b.scale;
    switch(b.shape) {
    case Shape::Circle: g.radius=22*b.scale;g.minx=g.miny=-g.radius;g.maxx=g.maxy=g.radius;return g;
    case Shape::Square:
        g.points={Vec{-21,-21},Vec{21,-21},Vec{21,21},Vec{-21,21}};
        g.count=4; g.axisCount=2; g.radius=29.69848481; break;
    case Shape::Rectangle:
        g.points={Vec{-30,-15},Vec{30,-15},Vec{30,15},Vec{-30,15}};
        g.count=4; g.axisCount=2; g.radius=33.54101967; break;
    case Shape::Triangle:
        g.points={Vec{0,-29},Vec{25.1147367,14.5},Vec{-25.1147367,14.5},Vec{}};
        g.count=3; g.axisCount=3; g.radius=29; break;
    }
    const double c=std::cos(b.angle), s=std::sin(b.angle);
    g.radius*=b.scale;
    g.minx=g.miny=1e9; g.maxx=g.maxy=-1e9;
    for(int i=0;i<g.count;++i) {
        Vec& p=g.points[i]; p=Vec{p.x*c-p.y*s,p.x*s+p.y*c}*b.scale;
        g.minx=std::min(g.minx,p.x); g.maxx=std::max(g.maxx,p.x);
        g.miny=std::min(g.miny,p.y); g.maxy=std::max(g.maxy,p.y);
    }
    for(int i=0;i<g.axisCount;++i) {
        Vec e=g.points[(i+1)%g.count]-g.points[i];
        g.axes[i]=Vec{-e.y,e.x}*(1.0/e.length());
    }
    return g;
}
inline void project(const Geometry& g, Vec center, Vec n, double& low, double& high) {
    const double origin=center.dot(n);
    if(g.count==0) { low=origin-g.radius; high=origin+g.radius; return; }
    low=high=g.points[0].dot(n);
    for(int i=1;i<g.count;++i) {
        const double p=g.points[i].dot(n); low=std::min(low,p); high=std::max(high,p);
    }
    low+=origin; high+=origin;
}
inline bool overlap(const Body& a, const Geometry& ag, const Body& b, const Geometry& bg, Vec& normal, double& depth) {
    const Vec delta=b.position-a.position;
    const double distance2=delta.dot(delta), radius=ag.radius+bg.radius;
    if(distance2>=radius*radius) return false;
    if(ag.count==0 && bg.count==0) {
        const double distance=std::sqrt(distance2);
        normal=distance>1e-9?delta*(1.0/distance):Vec{1,0}; depth=radius-distance; return true;
    }
    depth=1e9;
    const auto testAxis=[&](Vec n) {
        double amin,amax,bmin,bmax;
        project(ag,a.position,n,amin,amax); project(bg,b.position,n,bmin,bmax);
        const double d=std::min(amax-bmin,bmax-amin);
        if(d<=0) return false;
        if(d<depth) { depth=d; normal=delta.dot(n)<0?n*(-1):n; }
        return true;
    };
    for(int i=0;i<ag.axisCount;++i) if(!testAxis(ag.axes[i])) return false;
    for(int i=0;i<bg.axisCount;++i) if(!testAxis(bg.axes[i])) return false;
    // A circle also needs the axis toward the polygon's closest vertex.
    if(ag.count==0 || bg.count==0) {
        const Geometry& poly=ag.count==0?bg:ag;
        const Vec circleRelative=ag.count==0?a.position-b.position:b.position-a.position;
        Vec nearest=poly.points[0]-circleRelative; double nearest2=nearest.dot(nearest);
        for(int i=1;i<poly.count;++i) {
            Vec v=poly.points[i]-circleRelative; const double d=v.dot(v);
            if(d<nearest2) { nearest=v; nearest2=d; }
        }
        if(nearest2>1e-12 && !testAxis(nearest*(1.0/std::sqrt(nearest2)))) return false;
    }
    return true;
}
}
inline std::vector<Vec> vertices(const Body& b) {
    std::vector<Vec> points;
    if(b.shape==Shape::Circle) {
        points.reserve(20);
        for(int i=0;i<20;++i) {
            const double t=b.angle+i*detail::Tau/20;
            points.push_back(b.position+Vec{22*std::cos(t),22*std::sin(t)}*b.scale);
        }
    } else {
        const auto g=detail::geometry(b); points.reserve(g.count);
        for(int i=0;i<g.count;++i) points.push_back(b.position+g.points[i]);
    }
    return points;
}
inline bool overlap(const Body& a, const Body& b, Vec& normal, double& depth) {
    return detail::overlap(a,detail::geometry(a),b,detail::geometry(b),normal,depth);
}
// Stationary arena actors never consume a viewer body or enter body contacts.
struct StaticObstacle {
    Vec position{};
    detail::Geometry geometry{};
    bool active=true;
    static StaticObstacle polygon(Vec center,const std::array<Vec,6>& vertices,int count) {
        StaticObstacle obstacle;obstacle.position=center;auto& g=obstacle.geometry;
        g.points=vertices;g.count=g.axisCount=std::clamp(count,3,6);g.radius=0;
        g.minx=g.miny=1e9;g.maxx=g.maxy=-1e9;
        for(int i=0;i<g.count;++i){const Vec p=g.points[i];g.radius=std::max(g.radius,p.length());
            g.minx=std::min(g.minx,p.x);g.maxx=std::max(g.maxx,p.x);g.miny=std::min(g.miny,p.y);g.maxy=std::max(g.maxy,p.y);
            const Vec edge=g.points[(i+1)%g.count]-p;const double length=edge.length();
            g.axes[i]=length>1e-9?Vec{-edge.y,edge.x}*(1/length):Vec{1,0};}
        return obstacle;
    }
    static StaticObstacle box(Vec center,double halfExtent) {
        return polygon(center,{Vec{-halfExtent,-halfExtent},Vec{halfExtent,-halfExtent},Vec{halfExtent,halfExtent},Vec{-halfExtent,halfExtent}},4);
    }
    static StaticObstacle hexagon(Vec center,double radius) {
        std::array<Vec,6> points{};
        for(int i=0;i<6;++i){const double a=i*detail::Tau/6;points[i]={radius*std::cos(a),radius*std::sin(a)};}
        return polygon(center,points,6);
    }
};
class World {
public:
    static constexpr double Size=2000;
    static constexpr int Capacity=501;
    // Keep indices and ids stable: deactivate bodies; never erase/reorder them.
    // After external position/active changes, call rebuildSpatial once per batch.
    std::vector<Body> bodies;
    std::vector<StaticObstacle> obstacles;
    // Unique body INDEX pairs touched during the most recent step call.
    std::vector<std::pair<int,int>> contacts;

    World() : heads(GridWidth*GridWidth,-1),obstacleCells(GridWidth*GridWidth) {
        bodies.reserve(Capacity); cached.reserve(Capacity); next.reserve(Capacity);
        ids.reserve(Capacity); contacts.reserve(MaxContacts);
        obstacles.reserve(256);occupiedObstacleCells.reserve(1024);
    }
    Body* find(int id) {
        const auto it=ids.find(id); return it==ids.end()?nullptr:&bodies[it->second];
    }
    const Body* find(int id) const {
        const auto it=ids.find(id); return it==ids.end()?nullptr:&bodies[it->second];
    }
    bool add(int id, Shape shape,bool allowExtraSeat=false) {
        if(id<0 || !detail::validShape(shape) || bodies.size()>=static_cast<size_t>(allowExtraSeat?Capacity:Capacity-1) || ids.find(id)!=ids.end()) return false;
        Body body; body.id=id; body.shape=shape;
        if(!place(body)) return false;
        const double direction=random()*detail::Tau;
        body.velocity={std::cos(direction)*165,std::sin(direction)*165};
        body.spin=(random()-.5)*.5;
        const int index=static_cast<int>(bodies.size());
        bodies.push_back(body); cached.push_back(detail::geometry(body)); next.push_back(-1);
        ids.emplace(id,index); insert(index); return true;
    }
    bool reshape(int id, Shape shape) {
        Body* b=find(id);
        if(!b || !detail::validShape(shape)) return false;
        Body candidate=*b; candidate.shape=shape;
        if(candidate.active && !free(candidate) && !place(candidate)) return false;
        *b=candidate;
        cached[ids.find(id)->second]=detail::geometry(candidate);
        rebuildSpatial(); return true;
    }
    // Exact center-distance radius query. Caller can retain out's capacity.
    // Inactive entries are filtered even when just deactivated before a rebuild.
    void query(Vec center, double radius, std::vector<int>& out) const {
        out.clear();
        if(radius<0 || !std::isfinite(radius) || !std::isfinite(center.x) || !std::isfinite(center.y)) return;
        if(center.x+radius<0 || center.y+radius<0 || center.x-radius>Size || center.y-radius>Size) return;
        const int minx=cell(center.x-radius), maxx=cell(center.x+radius);
        const int miny=cell(center.y-radius), maxy=cell(center.y+radius);
        const double r2=radius*radius;
        for(int y=miny;y<=maxy;++y) for(int x=minx;x<=maxx;++x)
            for(int i=heads[y*GridWidth+x];i!=-1;i=next[i]) {
                const Body& b=bodies[i]; const Vec d=b.position-center;
                if(b.active && d.dot(d)<=r2) out.push_back(i);
            }
    }
    // Bounded sampling for staggered AI acquisition. Searches central cells first;
    // results are within radius but are neither exhaustive nor guaranteed nearest.
    // At most 4*maxResults body entries are visited, even in a coincident crowd.
    void queryLimited(Vec center, double radius, std::vector<int>& out, size_t maxResults=128) const {
        out.clear();
        if(maxResults==0 || radius<0 || !std::isfinite(radius) || !std::isfinite(center.x) || !std::isfinite(center.y)) return;
        if(center.x+radius<0 || center.y+radius<0 || center.x-radius>Size || center.y-radius>Size) return;
        maxResults=std::min(maxResults,static_cast<size_t>(Capacity));
        const size_t maxVisits=maxResults*4;
        const int cx=cell(center.x),cy=cell(center.y);
        const int rings=static_cast<int>(std::min(static_cast<double>(GridWidth-1),radius/CellSize+1));
        const double r2=radius*radius;
        size_t visits=0;
        const auto visitCell=[&](int x,int y) {
            if(x<0 || y<0 || x>=GridWidth || y>=GridWidth) return false;
            for(int i=heads[y*GridWidth+x];i!=-1;i=next[i]) {
                ++visits;
                const Body& b=bodies[i]; const Vec d=b.position-center;
                if(b.active && d.dot(d)<=r2) out.push_back(i);
                if(out.size()>=maxResults || visits>=maxVisits) return true;
            }
            return false;
        };
        if(visitCell(cx,cy)) return;
        for(int ring=1;ring<=rings;++ring) {
            for(int x=cx-ring;x<=cx+ring;++x)
                if(visitCell(x,cy-ring) || visitCell(x,cy+ring)) return;
            for(int y=cy-ring+1;y<cy+ring;++y)
                if(visitCell(cx-ring,y) || visitCell(cx+ring,y)) return;
        }
    }
    // Target acquisition must not let a crowd of allies hide the first enemy.
    // Traverse central cells first; only accepted bodies consume the limit.
    template<typename Predicate>
    void queryLimitedFiltered(Vec center,double radius,std::vector<int>& out,Predicate predicate,size_t maxResults=128) const {
        out.clear();
        if(maxResults==0||radius<0||!std::isfinite(radius)||!std::isfinite(center.x)||!std::isfinite(center.y))return;
        if(center.x+radius<0||center.y+radius<0||center.x-radius>Size||center.y-radius>Size)return;
        maxResults=std::min(maxResults,static_cast<size_t>(Capacity));
        const int cx=cell(center.x),cy=cell(center.y);
        const int rings=static_cast<int>(std::min(static_cast<double>(GridWidth-1),radius/CellSize+1));
        const double r2=radius*radius;
        const auto visitCell=[&](int x,int y){
            if(x<0||y<0||x>=GridWidth||y>=GridWidth)return false;
            for(int i=heads[y*GridWidth+x];i!=-1;i=next[i]){const auto& body=bodies[i];const Vec d=body.position-center;
                if(body.active&&d.dot(d)<=r2&&predicate(i)){out.push_back(i);if(out.size()>=maxResults)return true;}}
            return false;
        };
        if(visitCell(cx,cy))return;
        for(int ring=1;ring<=rings;++ring){
            for(int x=cx-ring;x<=cx+ring;++x)if(visitCell(x,cy-ring)||visitCell(x,cy+ring))return;
            for(int y=cy-ring+1;y<cy+ring;++y)if(visitCell(cx-ring,y)||visitCell(cx+ring,y))return;
        }
    }
    void rebuildSpatial() {
        std::fill(heads.begin(),heads.end(),-1);
        // Rotating insertion order prevents permanently favoring one crowd subset.
        const int count=static_cast<int>(bodies.size());
        if(count==0) return;
        const int start=static_cast<int>(serial%static_cast<uint64_t>(count));
        for(int k=0;k<count;++k) {
            const int i=(k+start)%count;
            next[i]=-1;
            if(bodies[i].active) insert(i);
        }
    }
    // Rebuild once after the caller batches obstacle creation/removal. Insert
    // occupied extents, so both large boss faces and small boxes are discoverable.
    void rebuildObstacleSpatial() {
        for(int index:occupiedObstacleCells)obstacleCells[index].clear();
        occupiedObstacleCells.clear();obstacleVisited.assign(obstacles.size(),0);obstacleQuerySerial=0;
        for(size_t i=0;i<obstacles.size();++i){const auto& o=obstacles[i];if(!o.active)continue;const auto& g=o.geometry;
            for(int y=cell(o.position.y+g.miny);y<=cell(o.position.y+g.maxy);++y)
                for(int x=cell(o.position.x+g.minx);x<=cell(o.position.x+g.maxx);++x){const int index=y*GridWidth+x;
                    if(obstacleCells[index].empty())occupiedObstacleCells.push_back(index);
                    obstacleCells[index].push_back(static_cast<int>(i));}}
    }
    void step(double dt) {
        contacts.clear();
        if(!std::isfinite(dt) || dt<=0) return;
        accumulator+=std::min(dt,.25);
        constexpr double fixed=1.0/120;
        while(accumulator+1e-10>=fixed) { integrate(fixed); accumulator-=fixed; }
        std::sort(contacts.begin(),contacts.end());
        contacts.erase(std::unique(contacts.begin(),contacts.end()),contacts.end());
    }
    void reset() {
        bodies.clear(); contacts.clear(); cached.clear(); next.clear(); ids.clear();
        obstacles.clear();rebuildObstacleSpatial();
        std::fill(heads.begin(),heads.end(),-1);
        accumulator=0; seed=0xC0FFEEu; serial=0; placementCursor=0;
    }
private:
    static constexpr double CellSize=80;
    static constexpr int GridWidth=static_cast<int>((Size+CellSize-1)/CellSize);
    static constexpr size_t MaxContacts=Capacity*16;
    // Two solver passes, at most 64 visited neighbors per body per pass.
    // In extremely crowded cells this deliberately samples contacts rather than
    // solving every pair. It bounds work to O(n), allows temporary penetration,
    // and rotates traversal each substep for fairness. Radius queries stay exact.
    static constexpr int MaxNeighborVisits=64;
    std::vector<int> heads, next,largeBodies;
    std::vector<detail::Geometry> cached;
    std::vector<std::vector<int>> obstacleCells;
    std::vector<int> occupiedObstacleCells;
    std::vector<uint64_t> obstacleVisited;
    uint64_t obstacleQuerySerial=0;
    std::unordered_map<int,int> ids;
    double accumulator=0;
    uint32_t seed=0xC0FFEEu;
    uint64_t serial=0;
    int placementCursor=0;
    static int cell(double p) { return static_cast<int>(std::clamp(p/CellSize,0.0,static_cast<double>(GridWidth-1))); }
    void insert(int index) {
        const Vec p=bodies[index].position; const int c=cell(p.y)*GridWidth+cell(p.x);
        next[index]=heads[c]; heads[c]=index;
    }
    double random() { seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return static_cast<double>(seed)/4294967295.0; }
    bool free(const Body& b,bool keepSpawnSpacing=true) const {
        const auto g=detail::geometry(b);
        if(b.position.x+g.minx<4 || b.position.y+g.miny<4 || b.position.x+g.maxx>Size-4 || b.position.y+g.maxy>Size-4) return false;
        const int minx=cell(b.position.x-72), maxx=cell(b.position.x+72);
        const int miny=cell(b.position.y-72), maxy=cell(b.position.y+72);
        if(keepSpawnSpacing) {
            for(int y=miny;y<=maxy;++y) for(int x=minx;x<=maxx;++x)
                for(int i=heads[y*GridWidth+x];i!=-1;i=next[i]) {
                    const Body& other=bodies[i]; const Vec delta=other.position-b.position;
                    if(other.active && other.id!=b.id && delta.dot(delta)<72*72) return false;
                }
        } else {
            // Dense admission may exhaust the preferred72-unit center spacing.
            // Use actual physical shapes at fallback sites, retaining their size.
            for(const auto& other:bodies)if(other.active&&other.id!=b.id) {
                Vec normal;double depth;
                if(detail::overlap(b,g,other,detail::geometry(other),normal,depth))return false;
            }
        }
        for(int y=cell(b.position.y+g.miny);y<=cell(b.position.y+g.maxy);++y)
            for(int x=cell(b.position.x+g.minx);x<=cell(b.position.x+g.maxx);++x)
                for(int i:obstacleCells[y*GridWidth+x]){const auto& o=obstacles[i];if(!o.active)continue;
                    Body proxy;proxy.position=o.position;Vec n;double depth;
                    if(detail::overlap(b,g,proxy,o.geometry,n,depth))return false;}
        return true;
    }
    bool place(Body& b) {
        for(int i=0;i<128;++i) {
            b.position={40+random()*(Size-80),40+random()*(Size-80)};
            if(free(b)) return true;
        }
        constexpr int side=static_cast<int>((Size-80)/76)+1;
        for(int i=0;i<side*side;++i) {
            const int slot=placementCursor++%(side*side);
            b.position={40.0+76*(slot%side),40.0+76*(slot/side)};
            if(free(b,false)) return true;
        }
        return false;
    }
    static void walls(Body& b, const detail::Geometry& g) {
        if(b.position.x+g.minx<0) { b.position.x=-g.minx; b.velocity.x=std::abs(b.velocity.x); }
        if(b.position.y+g.miny<0) { b.position.y=-g.miny; b.velocity.y=std::abs(b.velocity.y); }
        if(b.position.x+g.maxx>Size) { b.position.x=Size-g.maxx; b.velocity.x=-std::abs(b.velocity.x); }
        if(b.position.y+g.maxy>Size) { b.position.y=Size-g.maxy; b.velocity.y=-std::abs(b.velocity.y); }
    }
    void resolveObstacles(Body& body,const detail::Geometry& geometry) {
        if(occupiedObstacleCells.empty())return;
        // Requery after correction, which can cross a cell or touch a second
        // obstacle. Dedup within each query because polygons span many cells.
        for(int pass=0;pass<2;++pass){bool corrected=false;
            if(++obstacleQuerySerial==0){std::fill(obstacleVisited.begin(),obstacleVisited.end(),0);++obstacleQuerySerial;}
            const int minx=cell(body.position.x+geometry.minx),maxx=cell(body.position.x+geometry.maxx);
            const int miny=cell(body.position.y+geometry.miny),maxy=cell(body.position.y+geometry.maxy);
            for(int y=miny;y<=maxy;++y)for(int x=minx;x<=maxx;++x)
                for(int index:obstacleCells[y*GridWidth+x]){
                    if(obstacleVisited[index]==obstacleQuerySerial)continue;obstacleVisited[index]=obstacleQuerySerial;
                    const auto& obstacle=obstacles[index];if(!obstacle.active)continue;
                    Body proxy;proxy.position=obstacle.position;Vec normal;double depth;
                    if(!detail::overlap(body,geometry,proxy,obstacle.geometry,normal,depth))continue;
                    body.position+=normal*(-depth-.01);
                    const double toward=body.velocity.dot(normal);
                    if(toward>0)body.velocity+=normal*(-2*toward);
                    corrected=true;
                }
            if(!corrected)break;
        }
    }
    void integrate(double dt) {
        serial+=37;
        largeBodies.clear();
        for(size_t i=0;i<bodies.size();++i) {
            Body& b=bodies[i]; if(!b.active) continue;
            if(b.scale>1)largeBodies.push_back(static_cast<int>(i));
            const double speed=b.velocity.length();
            if(speed>280*b.speedScale) b.velocity=b.velocity*(280*b.speedScale/speed);
            b.position+=b.velocity*dt;
            b.angle=std::remainder(b.angle+b.spin*dt,detail::Tau);
            if(cached[i].shape!=b.shape || cached[i].angle!=b.angle || cached[i].scale!=b.scale) cached[i]=detail::geometry(b);
            walls(b,cached[i]);
            resolveObstacles(b,cached[i]);
        }
        rebuildSpatial();
        for(int pass=0;pass<2;++pass) {
            for(size_t index=0;index<bodies.size();++index) {
                Body& a=bodies[index]; if(!a.active) continue;
                const int cx=cell(a.position.x), cy=cell(a.position.y);
                int visits=0;
                const auto resolve=[&](int j){
                    if(j<=static_cast<int>(index)||!bodies[j].active)return;
                    Body& b=bodies[j];Vec n;double depth;
                    if(!detail::overlap(a,cached[index],b,cached[j],n,depth))return;
                    if(contacts.size()<MaxContacts)contacts.emplace_back(static_cast<int>(index),j);
                    const Vec correction=n*((depth+.01)*.5);a.position+=correction*(-1);b.position+=correction;
                    const double speed=(b.velocity-a.velocity).dot(n);
                    if(speed<0){const Vec impulse=n*(-speed*.99);a.velocity+=impulse*(-1);b.velocity+=impulse;
                        const double tangent=(b.velocity-a.velocity).dot({-n.y,n.x});a.spin=std::clamp(a.spin+tangent*.002,-1.5,1.5);b.spin=std::clamp(b.spin-tangent*.002,-1.5,1.5);}
                };
                const int rings=a.scale>1?2:1;
                for(int ring=0;ring<=rings&&visits<MaxNeighborVisits;++ring)
                for(int y=std::max(0,cy-ring);y<=std::min(GridWidth-1,cy+ring) && visits<MaxNeighborVisits;++y)
                    for(int x=std::max(0,cx-ring);x<=std::min(GridWidth-1,cx+ring) && visits<MaxNeighborVisits;++x)if(std::max(std::abs(x-cx),std::abs(y-cy))==ring)
                        for(int j=heads[y*GridWidth+x];j!=-1 && visits<MaxNeighborVisits;j=next[j]) {
                            ++visits;
                            resolve(j);
                        }
                // Only the one extra host has a wider envelope. Handle its
                // rare second-cell contacts without widening all viewer queries.
                if(a.scale<=1)for(int j:largeBodies)if(std::max(std::abs(cell(bodies[j].position.x)-cx),std::abs(cell(bodies[j].position.y)-cy))>1)resolve(j);
            }
            for(size_t i=0;i<bodies.size();++i) if(bodies[i].active) {resolveObstacles(bodies[i],cached[i]);walls(bodies[i],cached[i]);}
            rebuildSpatial();
        }
        for(auto& b:bodies) if(b.active) {
            const double speed=b.velocity.length();
            if(speed<1e-6) b.velocity={90*b.speedScale,0};
            else if(speed<90*b.speedScale) b.velocity=b.velocity*(std::min(90*b.speedScale,speed+dt*45*b.speedScale)/speed);
            else if(speed>280*b.speedScale) b.velocity=b.velocity*(280*b.speedScale/speed);
            b.spin*=.999;
        }
    }
};
}
