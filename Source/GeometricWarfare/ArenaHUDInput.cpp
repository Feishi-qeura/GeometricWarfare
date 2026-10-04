#include "ArenaHUD.h"
#include "ArenaGameMode.h"
#include "Engine/World.h"

namespace {
bool Contains(const FBox2D& Bounds,const FVector2D& Point) {
    return Bounds.bIsValid && Bounds.IsInsideOrOn(Point);
}
bool ValidPointer(float X,float Y,float Width,float Height) {
    return FMath::IsFinite(X) && FMath::IsFinite(Y) && Width>0 && Height>0
        && X>=0 && Y>=0 && X<Width && Y<Height;
}
gw::ArenaView ReadView(const AArenaGameMode& Game) {
    return {Game.CameraZoom,{Game.CameraCenter.X,Game.CameraCenter.Y}};
}
void WriteView(AArenaGameMode& Game,const gw::ArenaView& View) {
    Game.CameraZoom=static_cast<float>(View.zoom);
    Game.CameraCenter={View.center.x,View.center.y};
}
}

bool AArenaHUD::BeginPointer(float X,float Y) {
    CancelPointer();
    if(!ValidPointer(X,Y,ViewportWidth,ViewportHeight) || !GetWorld()->GetAuthGameMode<AArenaGameMode>()) return false;
    const FVector2D Point(X,Y);
    // The minimap can overlap the arena and always takes precedence.
    if(Contains(MinimapBounds,Point)) {
        PointerArea=EPointerArea::Minimap;
        PointerDrag.begin(X,Y); UpdatePointer(X,Y); return PointerDrag.down;
    }
    for(const auto& Region:RankRegions) if(Region.Id>=0 && Contains(Region.Bounds,Point)) {
        PointerArea=EPointerArea::Rank;
        PressedRankId=Region.Id; PressedRankBounds=Region.Bounds;
        PointerDrag.begin(X,Y); return true;
    }
    if(ArenaSide>0 && InArena(Point)) {
        PointerArea=EPointerArea::Arena;
        PointerDrag.begin(X,Y); return true;
    }
    return false;
}

void AArenaHUD::UpdatePointer(float X,float Y) {
    if(!PointerDrag.down || PointerArea==EPointerArea::None) return;
    auto* Game=GetWorld()->GetAuthGameMode<AArenaGameMode>();
    if(!Game || !ValidPointer(X,Y,ViewportWidth,ViewportHeight)) { CancelPointer(); return; }
    const gw::Vec Delta=PointerDrag.update(X,Y);
    // Keep tracking movement so a blocked drag never turns into a click. Only
    // right click releases follow; map/minimap gestures cannot move a locked view.
    if(Game->FocusBodyId>=0) return;
    if(PointerArea==EPointerArea::Minimap) {
        if(!MinimapBounds.bIsValid) { CancelPointer(); return; }
        const FVector2D Size=MinimapBounds.GetSize();
        if(Size.X<=0 || Size.Y<=0) { CancelPointer(); return; }
        auto View=ReadView(*Game);
        View.jumpNormalized((X-MinimapBounds.Min.X)/Size.X,(Y-MinimapBounds.Min.Y)/Size.Y);
        WriteView(*Game,View);
    } else if(PointerArea==EPointerArea::Arena && PointerDrag.dragging) {
        auto View=ReadView(*Game);
        View.panPixels(Delta.x,Delta.y,ArenaSide);
        WriteView(*Game,View);
    }
}

void AArenaHUD::EndPointer(float X,float Y) {
    // Consume release motion even if press and release happened between ticks.
    UpdatePointer(X,Y);
    if(!PointerDrag.down) return;
    const EPointerArea Area=PointerArea;
    const int32 RankId=PressedRankId;
    const bool bWithinPressedRank=Contains(PressedRankBounds,{X,Y});
    const bool bClick=PointerDrag.finish();
    CancelPointer();
    auto* Game=GetWorld()->GetAuthGameMode<AArenaGameMode>();
    if(!Game || !bClick) return;
    if(Area==EPointerArea::Rank) {
        // Use the ID captured on press, even when the leaderboard has re-sorted.
        if(RankId>=0 && bWithinPressedRank) Game->FocusViewer(RankId);
    } else if(Area==EPointerArea::Arena && ArenaSide>0 && InArena({X,Y}) && !Contains(MinimapBounds,{X,Y})) {
        const auto View=ReadView(*Game);
        const gw::Vec Position=View.screenToWorld((X-ArenaX)/ArenaSide,(Y-ArenaY)/ArenaSide);
        const double Radius=24.0*View.visibleSize()/ArenaSide;
        std::vector<int> Candidates;
        const auto& World=Game->GetArena();
        World.query(Position,Radius,Candidates);
        int32 Id=-1; double Best=Radius*Radius;
        for(int Index:Candidates) {
            const auto& Body=World.bodies[Index];
            if(!Body.active) continue;
            const auto Difference=Body.position-Position;
            const double Distance=Difference.dot(Difference);
            if(Distance<Best || (Distance==Best && (Id<0 || Body.id<Id))) { Best=Distance; Id=Body.id; }
        }
        if(Id>=0) Game->FocusViewer(Id);
    }
}

void AArenaHUD::CancelPointer() {
    PointerDrag.cancel(); PointerArea=EPointerArea::None;
    PressedRankId=-1; PressedRankBounds=FBox2D(ForceInit);
}

void AArenaHUD::ZoomAtCursor(float X,float Y,float Factor) {
    if(!ValidPointer(X,Y,ViewportWidth,ViewportHeight) || !FMath::IsFinite(Factor) || Factor<=0)return;
    if(Contains(RankBounds,{X,Y})) {
        // Scrolling invalidates a pending click, while ordinary live re-sorts
        // still select the stable identity captured on press.
        if(Factor!=1){CancelPointer();RankOffset=FMath::Clamp(RankOffset+(Factor>1?-1:1),0,FMath::Max(0,RankTotalRows-RankVisibleRows));RankRegions.Reset();}
        return;
    }
    if(ArenaSide<=0 || !InArena({X,Y}) || Contains(MinimapBounds,{X,Y})) return;
    auto* Game=GetWorld()->GetAuthGameMode<AArenaGameMode>();
    if(!Game) return;
    auto View=ReadView(*Game);
    if(Game->FocusBodyId>=0) {
        View.zoomAt((X-ArenaX)/ArenaSide,(Y-ArenaY)/ArenaSide,Factor,true);
        Game->CameraZoom=static_cast<float>(View.zoom);
        return;
    }
    View.zoomAt((X-ArenaX)/ArenaSide,(Y-ArenaY)/ArenaSide,Factor);
    WriteView(*Game,View);
}

void AArenaHUD::SelectAtCursor(float X,float Y) {
    if(BeginPointer(X,Y)) EndPointer(X,Y);
}
