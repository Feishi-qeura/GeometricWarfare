#include "ArenaHUD.h"
#include "ArenaGameMode.h"
#include "UI/ArenaIcons.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"

namespace {
const FLinearColor CombatInk(.035f,.065f,.08f,1),CombatGold(.88f,.49f,.055f,1),CombatRed(.93f,.045f,.025f,1);
FLinearColor CombatTeam(int32 T) {return T==1?FLinearColor(.88f,.10f,.06f,1):T==2?FLinearColor(.055f,.35f,.86f,1):FLinearColor(.43f,.49f,.45f,1);}
double PointViewDistance(FVector2D P,FVector2D Min,FVector2D Max) {
    const double X=FMath::Max(FMath::Max(Min.X-P.X,0.),P.X-Max.X),Y=FMath::Max(FMath::Max(Min.Y-P.Y,0.),P.Y-Max.Y);
    return FMath::Sqrt(X*X+Y*Y);
}
double SegmentViewDistance(FVector2D A,FVector2D B,FVector2D Min,FVector2D Max) {
    const FVector2D D=B-A;double Lo=0,Hi=1;
    const auto Clip=[&](double P,double Q){if(FMath::Abs(P)<1e-9)return Q>=0;const double T=Q/P;if(P<0)Lo=FMath::Max(Lo,T);else Hi=FMath::Min(Hi,T);return Lo<=Hi;};
    if(Clip(-D.X,A.X-Min.X)&&Clip(D.X,Max.X-A.X)&&Clip(-D.Y,A.Y-Min.Y)&&Clip(D.Y,Max.Y-A.Y))return 0;
    double Distance=FMath::Min(PointViewDistance(A,Min,Max),PointViewDistance(B,Min,Max));
    const FVector2D Corners[]={Min,{Max.X,Min.Y},Max,{Min.X,Max.Y}};
    for(const auto P:Corners){const double T=D.SizeSquared()>1e-9?FMath::Clamp(FVector2D::DotProduct(P-A,D)/D.SizeSquared(),0.,1.):0;Distance=FMath::Min(Distance,(P-A-D*T).Size());}
    return Distance;
}
}

void AArenaHUD::UpdateArenaShake(AArenaGameMode* G) {
    ArenaShakeOffset=FVector2D::ZeroVector;
    if(G->IsSimulationPaused() || G->GetMatch().phase==gw::Phase::Results || WorldScale<=0)return;
    const auto& B=G->GetMatch().boss;
    const FVector2D Min=ViewOrigin,Max=Min+FVector2D(ArenaSide/WorldScale);
    const auto Near=[&](gw::Vec P,double Radius){return FMath::Clamp(1.-PointViewDistance({P.x,P.y},Min,Max)/Radius,0.,1.);};
    float Strength=0;
    if(B.active && B.attack==gw::BossAttack::LaserActive) {
        const double Distance=SegmentViewDistance({B.laserFrom.x,B.laserFrom.y},{B.laserTo.x,B.laserTo.y},Min,Max);
        const double Proximity=FMath::Max(FMath::Clamp(1.-Distance/450.,0.,1.),Near(B.position,650));
        const double Growth=gw::BossLaserWidthAt(B.attackElapsed)/gw::BossLaserWidth;
        const double Fade=FMath::Clamp(B.attackRemaining/.18,0.,1.);
        Strength=1.8f*Proximity*Growth*Fade;
    }
    if(B.active && B.attack==gw::BossAttack::StompWave)
        Strength+=2.1f*Near(B.waveCenter,gw::BossWaveRadius)*FMath::Clamp(1.-B.attackElapsed/.28,0.,1.);
    for(const auto& E:B.explosions)if(E.active)
        Strength+=2.4f*Near(E.position,E.radius+350)*FMath::Pow(FMath::Clamp(1.-E.age/E.lifetime,0.,1.),2.);
    Strength=FMath::Min(Strength,3.f)*FMath::Clamp(WorldScale/.5f,.2f,1.f);
    const float Time=G->RunningTime;
    ArenaShakeOffset=FVector2D(FMath::Sin(Time*67)+.35f*FMath::Sin(Time*113),FMath::Cos(Time*79)+.3f*FMath::Sin(Time*101))*(Strength/1.35f);
}

void AArenaHUD::DrawStatusAura(FVector2D Center,float HalfSize,bool Evolution,bool BossBuff,float Time,bool ClipToArena,bool Hero) {
    if(!Evolution && !BossBuff && !Hero)return;
    const float Breath=.5f+.5f*FMath::Sin(Time*3.1f),Size=HalfSize*(.98f+.045f*Breath);
    const float Stroke=FMath::Clamp(HalfSize*.07f,.9f,1.8f),Glow=Stroke+FMath::Clamp(HalfSize*.17f,1.4f,4.f);
    // Submit each complete outline as one triangle batch, even with thousands of buffs.
    const auto Outline=[&](const auto& Points,FLinearColor Color,float Width) {
        FCanvasTriangleItem Item(Center,Center,Center,GWhiteTexture);Item.TriangleList.Reset();Item.TriangleList.Reserve(Points.Num()*2);
        const auto Triangle=[&](FVector2D A,FVector2D B,FVector2D C){FCanvasUVTri T{};T.V0_Pos=A;T.V1_Pos=B;T.V2_Pos=C;T.V0_UV=T.V1_UV=T.V2_UV=FVector2D::ZeroVector;Item.TriangleList.Add(T);};
        for(int32 i=0;i<Points.Num();++i) {
            const auto A=Points[i],B=Points[(i+1)%Points.Num()],D=(B-A).GetSafeNormal();const FVector2D N(-D.Y*Width*.5,D.X*Width*.5);
            if(ClipToArena && (!InArena(A,Width)||!InArena(B,Width)))ArenaPolygon({A+N,B+N,B-N,A-N},Color);
            else{Triangle(A+N,B+N,B-N);Triangle(A+N,B-N,A-N);}
        }
        if(Item.TriangleList.Num()>0){Item.SetColor(Color);Item.BlendMode=SE_BLEND_Translucent;Canvas->DrawItem(Item);}
    };
    if(Evolution) {
        const TArray<FVector2D> Square={Center+FVector2D(-Size,-Size),Center+FVector2D(Size,-Size),Center+FVector2D(Size,Size),Center+FVector2D(-Size,Size)};
        Outline(Square,FLinearColor(1,.34f,.025f,.10f+.12f*Breath),Glow);
        Outline(Square,FLinearColor(1,.34f,.025f,.52f+.4f*Breath),Stroke);
    }
    if(BossBuff) {
        // With both effects the purple circle encloses all four orange corners.
        const float Radius=Evolution?Size*1.414214f+Stroke*1.6f:Size*1.16f;
        const int32 Segments=HalfSize<7?12:32;
        TArray<FVector2D,TInlineAllocator<32>> Circle;
        for(int32 i=0;i<Segments;++i){const float A=i*2*PI/Segments;Circle.Add(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius);}
        Outline(Circle,FLinearColor(.57f,.17f,.95f,.09f+.11f*Breath),Glow);
        Outline(Circle,FLinearColor(.57f,.17f,.95f,.50f+.42f*Breath),Stroke);
    }
    if(Hero) {
        // A gold hexagon remains distinct outside the orange square and purple ring.
        const float Radius=Size*(Evolution&&BossBuff?1.88f:Evolution?1.65f:BossBuff?1.48f:1.25f);
        TArray<FVector2D,TInlineAllocator<6>> Hex;
        for(int32 i=0;i<6;++i){const float A=-PI/2+i*PI/3;Hex.Add(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius);}
        Outline(Hex,FLinearColor(1,.69f,.07f,.12f+.12f*Breath),Glow);
        Outline(Hex,FLinearColor(.89f,.54f,.035f,.78f+.20f*Breath),Stroke+0.3f);
    }
}

// Clip every large effect to the playfield, including beams whose endpoints
// are both offscreen. Keep Canvas draw calls away from the surrounding HUD.
void AArenaHUD::ArenaLine(FVector2D A,FVector2D B,FLinearColor Color,float Width) {
    if(Width<=0 || Color.A<=0 || (B-A).IsNearlyZero())return;
    if(InArena(A,Width*.5f)&&InArena(B,Width*.5f)){Line(A,B,Color,Width);return;}
    const double Pad=Width*.5;
    if(!ClipArenaRect(FBox2D({FMath::Min(A.X,B.X)-Pad,FMath::Min(A.Y,B.Y)-Pad},{FMath::Max(A.X,B.X)+Pad,FMath::Max(A.Y,B.Y)+Pad})).bIsValid)return;
    const auto D=(B-A).GetSafeNormal();const FVector2D N(-D.Y*Width*.5,D.X*Width*.5);
    // Clip the stroke itself: shrinking the rectangle can hide a visible edge.
    ArenaPolygon({A+N,B+N,B-N,A-N},Color);
}
void AArenaHUD::ArenaPolygon(const TArray<FVector2D>& Points,FLinearColor Color) {
    if(Points.Num()<3 || Color.A<=0)return;
    FBox2D Bounds(ForceInit);bool EntirelyInside=true;
    for(const auto Point:Points){Bounds+=Point;EntirelyInside&=InArena(Point);}
    if(!ClipArenaRect(Bounds).bIsValid)return;
    if(EntirelyInside){Polygon(Points,Color);return;}
    TArray<FVector2D> P=Points,Q;
    for(int32 Edge=0;Edge<4 && P.Num()>2;++Edge) {
        Q.Reset();const bool Vertical=Edge<2;const double Bound=Edge==0?ArenaX:Edge==1?ArenaX+ArenaSide:Edge==2?ArenaY:ArenaY+ArenaSide;
        auto Coordinate=[Vertical](FVector2D V){return Vertical?V.X:V.Y;};
        auto Inside=[&](FVector2D V){return (Edge==0||Edge==2)?Coordinate(V)>=Bound:Coordinate(V)<=Bound;};
        FVector2D A=P.Last();bool AInside=Inside(A);
        for(const auto B:P) {
            const bool BInside=Inside(B);
            if(AInside!=BInside){const double Den=Coordinate(B)-Coordinate(A);if(FMath::Abs(Den)>1e-9)Q.Add(A+(B-A)*((Bound-Coordinate(A))/Den));}
            if(BInside)Q.Add(B);A=B;AInside=BInside;
        }
        P=MoveTemp(Q);
    }
    if(P.Num()>2)Polygon(P,Color);
}
void AArenaHUD::ArenaRing(FVector2D Center,float Radius,FLinearColor Color,float Width,int32 Segments) {
    if(Radius<=0)return;
    for(int32 i=0;i<Segments;++i){const float A=i*2*PI/Segments,B=(i+1)*2*PI/Segments;ArenaLine(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius,Center+FVector2D(FMath::Cos(B),FMath::Sin(B))*Radius,Color,Width);}
}
void AArenaHUD::DrawCombatGround(AArenaGameMode* G) {
    const auto& M=G->GetMatch();const auto& B=M.boss;
    for(const auto& Explosion:M.explosions)if(Explosion.active) {
        const auto P=Project(Explosion.position.x,Explosion.position.y);
        const float Progress=FMath::Clamp(static_cast<float>(Explosion.age/Explosion.lifetime),0.f,1.f),Fade=1-Progress;
        const float Radius=Explosion.radius*WorldScale,WaveRadius=Radius*(.15f+.85f*FMath::Sqrt(Progress));
        auto C=CombatTeam(Explosion.team);C.A=Fade*.12f;
        TArray<FVector2D> Disc;for(int32 i=0;i<24;++i)Disc.Add(P+FVector2D(FMath::Cos(i*PI/12),FMath::Sin(i*PI/12))*Radius);
        ArenaPolygon(Disc,C);C.A=Fade*.25f;ArenaRing(P,Radius,C,1,24);
        C.A=Fade*.85f;ArenaRing(P,WaveRadius,C,FMath::Max(1.f,3*WorldScale),24);
    }
    for(const auto& Scorch:B.scorches)if(Scorch.active) {
        const auto A=Project(Scorch.from.x,Scorch.from.y),Z=Project(Scorch.to.x,Scorch.to.y);
        const float Width=gw::BossScorchWidthAt(Scorch.age)*WorldScale,Fade=gw::BossScorchOpacityAt(Scorch.age,Scorch.lifetime);
        const FVector2D D(Scorch.direction.x,Scorch.direction.y),N(-D.Y,D.X);
        ArenaLine(A,Z,FLinearColor(.33f,.12f,.035f,.25f*Fade),Width);
        ArenaLine(A,Z,FLinearColor(.99f,.27f,.025f,.16f*Fade),Width*.72f);
        ArenaLine(A+N*Width*.44f,Z+N*Width*.44f,FLinearColor(.94f,.31f,.04f,.45f*Fade),FMath::Max(.7f,1.1f*WorldScale));
        ArenaLine(A-N*Width*.44f,Z-N*Width*.44f,FLinearColor(.94f,.31f,.04f,.45f*Fade),FMath::Max(.7f,1.1f*WorldScale));
        for(int32 i=0;i<12;++i) {
            const float Phase=FMath::Fmod(G->RunningTime*.7f+i*.317f,1.f);
            const auto P=FMath::Lerp(A,Z,(i+.5f)/12.f)+N*(FMath::Sin(i*2.4f)*Width*.3f);
            ArenaLine(P,P+FVector2D(1,-(3+6*WorldScale)*Phase),FLinearColor(.94f,.24f,.025f,(1-Phase)*Fade*.6f),FMath::Max(.8f,1.4f*WorldScale));
        }
    }
    if(B.spawned && B.spawnAge<gw::BossSpawnWaveSeconds) {
        const auto P=Project(B.position.x,B.position.y);
        const float Radius=gw::BossSpawnWaveRadiusAt(B.spawnAge)*WorldScale,Fade=gw::BossSpawnWaveOpacityAt(B.spawnAge);
        const FLinearColor Gravity(.53f,.23f,.80f,1);
        ArenaRing(P,Radius,FLinearColor(Gravity.R,Gravity.G,Gravity.B,Fade*.16f),FMath::Max(3.f,24*WorldScale),72);
        ArenaRing(P,Radius,FLinearColor(Gravity.R,Gravity.G,Gravity.B,Fade*.8f),FMath::Max(1.2f,3*WorldScale),72);
        ArenaRing(P,Radius*.87f,FLinearColor(Gravity.R,Gravity.G,Gravity.B,Fade*.28f),FMath::Max(1.f,2*WorldScale),64);
        for(int32 i=0;i<24;++i){const float Angle=i*PI/12;const FVector2D D(FMath::Cos(Angle),FMath::Sin(Angle)),N(-D.Y,D.X);
            const auto Tip=P+D*Radius*.74;const float Length=FMath::Max(5.f,30*WorldScale);
            ArenaLine(Tip+D*Length,Tip,FLinearColor(Gravity.R,Gravity.G,Gravity.B,Fade*.5f),1.4f);
            ArenaLine(Tip,Tip+D*Length*.35+N*Length*.2,FLinearColor(Gravity.R,Gravity.G,Gravity.B,Fade*.5f),1.4f);
            ArenaLine(Tip,Tip+D*Length*.35-N*Length*.2,FLinearColor(Gravity.R,Gravity.G,Gravity.B,Fade*.5f),1.4f);
        }
    }
    if(!B.active)return;
    const auto Center=Project(B.fireCenter.x,B.fireCenter.y);const float R=gw::BossFireRadius*WorldScale;
    FLinearColor C=B.rage?CombatRed:CombatGold;C.A=.13f;
    TArray<FVector2D> Ground;for(int32 i=0;i<48;++i)Ground.Add(Center+FVector2D(FMath::Cos(i*PI/24),FMath::Sin(i*PI/24))*R);
    ArenaPolygon(Ground,C);C.A=.52f;ArenaRing(Center,R,C,1.5f,48);
    C.A=.15f;ArenaRing(Center,R*.70f,C,1,32);
    for(int32 i=0;i<18;++i) {
        const float Angle=i*2.399963f,Distance=R*FMath::Sqrt((i+.5f)/18.f);
        const auto P=Center+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*Distance;
        const float Phase=FMath::Fmod(G->RunningTime*.8f+i*.37f,1.f),Height=(5+7*WorldScale)*Phase;
        C.A=(1-Phase)*.45f;
        ArenaLine(P,P+FVector2D(2,-Height),C,1.4f);
    }
}
void AArenaHUD::DrawSupplyStatus(FVector2D Center,float Radius,double Hp,double MaxHp,double HitFlash,const FString& Label) {
    const float Hit=FMath::Clamp(static_cast<float>(HitFlash/.18),0.f,1.f);
    if(Hit>0) {
        const float Burst=Radius+3+(1-Hit)*FMath::Max(5.f,13*WorldScale);
        for(int32 I=0;I<4;++I){const float A=PI*.25f+I*PI*.5f;const FVector2D D(FMath::Cos(A),FMath::Sin(A));
            ArenaLine(Center+D*(Radius+2),Center+D*Burst,FLinearColor(CombatGold.R,CombatGold.G,CombatGold.B,Hit),FMath::Max(1.f,1.5f*WorldScale));}
    }
    const bool Close=WorldScale>.55f,Damaged=Hp<MaxHp-.01;
    if(Close) {
        const float Width=FMath::Max(54.f,Radius*2+8),Y=Center.Y-Radius-17;
        if(InArena({Center.X-Width*.5f,Y})&&InArena({Center.X+Width*.5f,Y+12}))HealthBar(Center.X-Width*.5f,Y,Width,12,Hp,MaxHp,CombatGold,8);
        if(InArena({Center.X-61,Center.Y+Radius+5})&&InArena({Center.X+61,Center.Y+Radius+18}))Text(Label,Center.X,Center.Y+Radius+5,9,CombatGold,true);
    } else if(Damaged) {
        // At overview scale retain a thin health cue without adding tiny labels.
        const float Width=FMath::Max(14.f,Radius*2+4),Y=Center.Y-Radius-5;
        ArenaRect(Center.X-Width*.5f,Y,Width,2,FLinearColor(.58f,.57f,.48f,.35f));
        ArenaRect(Center.X-Width*.5f,Y,Width*FMath::Clamp(Hp/FMath::Max(MaxHp,1.),0.,1.),2,CombatGold);
    }
}
void AArenaHUD::DrawCombatActors(AArenaGameMode* G) {
    const auto& M=G->GetMatch();
    for(const auto& Crate:M.weaponCrates)if(Crate.active) {
        const auto P=Project(Crate.position.x,Crate.position.y);const float R=FMath::Max(static_cast<float>(gw::PickupHalfExtent)*WorldScale,5.f);
        if(!ClipArenaRect(FBox2D(P-FVector2D(R),P+FVector2D(R))).bIsValid)continue;
        const float Hit=FMath::Clamp(static_cast<float>(Crate.hitFlash/.18),0.f,1.f);
        ArenaRect(P.X-R,P.Y-R,2*R,2*R,FMath::Lerp(FLinearColor(.99f,.94f,.78f,1),FLinearColor::White,Hit));
        const TArray<FVector2D> Corners={P+FVector2D(-R,-R),P+FVector2D(R,-R),P+FVector2D(R,R),P+FVector2D(-R,R)};
        for(int32 i=0;i<4;++i)ArenaLine(Corners[i],Corners[(i+1)%4],CombatGold,1.5f+Hit);
        ArenaLine(P+FVector2D(-R,-R*.55f),P+FVector2D(R,-R*.55f),CombatGold,1);
        ArenaLine(P+FVector2D(-R*.64f,-R),P+FVector2D(-R*.64f,R),CombatGold,1);
        ArenaLine(P+FVector2D(R*.64f,-R),P+FVector2D(R*.64f,R),CombatGold,1);
        if(InArena(P,R+2)) {
            const auto Icon=Crate.weaponKind==gw::WeaponKind::Sniper?gwui::Icon::Sniper:Crate.weaponKind==gw::WeaponKind::MachineGun?gwui::Icon::MachineGun:gwui::Icon::RocketLauncher;
            gwui::DrawIcon(Canvas,Icon,P+FVector2D(0,R*.12f),R*1.22f,CombatInk);
        }
        DrawSupplyStatus(P,R,Crate.hp,Crate.maxHp,Crate.hitFlash,TEXT("武器 60s · 击破或触碰"));
    }
    for(const auto& Pack:M.evolutionPacks)if(Pack.active) {
        const auto P=Project(Pack.position.x,Pack.position.y);const float R=FMath::Max(static_cast<float>(gw::PickupHalfExtent)*WorldScale,4.f);if(!ClipArenaRect(FBox2D(P-FVector2D(R),P+FVector2D(R))).bIsValid)continue;
        const float Hit=FMath::Clamp(static_cast<float>(Pack.hitFlash/.18),0.f,1.f);
        ArenaRect(P.X-R,P.Y-R,2*R,2*R,FMath::Lerp(FLinearColor(1,.93f,.59f,1),FLinearColor::White,Hit));
        ArenaLine(P+FVector2D(-R,-R),P+FVector2D(R,-R),CombatGold,2);ArenaLine(P+FVector2D(R,-R),P+FVector2D(R,R),CombatGold,2);
        ArenaLine(P+FVector2D(R,R),P+FVector2D(-R,R),CombatGold,2);ArenaLine(P+FVector2D(-R,R),P+FVector2D(-R,-R),CombatGold,2);
        if(InArena(P,R))gwui::DrawIcon(Canvas,gwui::Icon::Lightning,P,R*1.5f,CombatGold);
        DrawSupplyStatus(P,R,Pack.hp,Pack.maxHp,Pack.hitFlash,TEXT("进化 40s · 击破或触碰"));
    }
    for(const auto& Wave:M.swordWaves)if(Wave.active) {
        const auto P=Project(Wave.position.x,Wave.position.y);const FVector2D D(Wave.direction.x,Wave.direction.y),N(-D.Y,D.X);
        const float R=FMath::Max(5.f,24.f*WorldScale);auto C=Wave.hero?FLinearColor(.92f,.58f,.035f,1):FMath::Lerp(CombatTeam(Wave.team),FLinearColor(.8f,.95f,.92f,1),.25f);
        const double Remaining=FMath::Min(Wave.life,(Wave.maxDistance-Wave.traveled)/FMath::Max(Wave.speed,.001));
        C.A=FMath::Clamp(Remaining/.35,0.,1.);
        const auto A=P-N*R-D*R*.5,B=P+D*R*.28,E=P+N*R-D*R*.5;
        ArenaLine(A,B,FLinearColor(C.R,C.G,C.B,C.A*.22f),FMath::Max(4.f,R*.55f));ArenaLine(B,E,FLinearColor(C.R,C.G,C.B,C.A*.22f),FMath::Max(4.f,R*.55f));
        ArenaLine(A,B,C,2);ArenaLine(B,E,C,2);
    }
    const auto& B=M.boss;
    FLinearColor C=B.rage?CombatRed:CombatGold;
    for(const auto& Explosion:B.explosions)if(Explosion.active) {
        const auto P=Project(Explosion.position.x,Explosion.position.y);
        const float Progress=FMath::Clamp(static_cast<float>(Explosion.age/Explosion.lifetime),0.f,1.f),Fade=1-Progress;
        const float R=FMath::Max(8.f,static_cast<float>(Explosion.radius)*WorldScale)*(.12f+.88f*FMath::Sqrt(Progress));
        ArenaRing(P,R,FLinearColor(C.R,C.G,C.B,Fade*.22f),FMath::Max(2.f,R*.16f),24);
        ArenaRing(P,R*.73f,FLinearColor(C.R,C.G,C.B,Fade*.75f),1.5f,24);
        for(int32 i=0;i<8;++i){const float Angle=i*PI/4+.2f;const FVector2D D(FMath::Cos(Angle),FMath::Sin(Angle));ArenaLine(P+D*R*.36,P+D*R*.85,FLinearColor(C.R,C.G,C.B,Fade*Fade),FMath::Max(1.f,2*WorldScale));}
        if(Progress<.22f){const float Flash=FMath::Max(3.f,16*WorldScale)*(1-Progress/.22f);ArenaPolygon({P+FVector2D(0,-Flash),P+FVector2D(Flash,0),P+FVector2D(0,Flash),P+FVector2D(-Flash,0)},FLinearColor(C.R,C.G,C.B,Fade));}
    }
    if(!B.active)return;
    const auto Position=Project(B.position.x,B.position.y);
    if(B.attack==gw::BossAttack::LaserWindup || B.attack==gw::BossAttack::LaserActive) {
        const auto A=Project(B.laserFrom.x,B.laserFrom.y),Z=Project(B.laserTo.x,B.laserTo.y);
        const bool Firing=B.attack==gw::BossAttack::LaserActive;
        const FLinearColor LaserColor(.96f,.22f,.055f,1);
        if(Firing) {
            const float Width=gw::BossLaserWidthAt(B.attackElapsed)*WorldScale;
            const float Pulse=.94f+.06f*FMath::Sin(G->RunningTime*19);
            ArenaLine(A,Z,FLinearColor(LaserColor.R,LaserColor.G,LaserColor.B,.09f),Width*1.85f);
            ArenaLine(A,Z,FLinearColor(LaserColor.R,LaserColor.G,LaserColor.B,.20f),Width*1.35f);
            ArenaLine(A,Z,FLinearColor(LaserColor.R,LaserColor.G,LaserColor.B,.76f*Pulse),Width);
            ArenaLine(A,Z,FLinearColor(LaserColor.R,LaserColor.G,LaserColor.B,Pulse),FMath::Max(.8f,Width*.32f));
        }
        else {
            const float Width=gw::BossLaserWarningWidth*WorldScale;
            ArenaLine(A,Z,FLinearColor(LaserColor.R,LaserColor.G,LaserColor.B,.22f),Width);
            ArenaLine(A,Z,FLinearColor(LaserColor.R,LaserColor.G,LaserColor.B,.50f+.22f*FMath::Sin(G->RunningTime*9)),FMath::Max(.7f,Width*.22f));
        }
    }
    if(B.attack==gw::BossAttack::StompWave) {
        const auto P=Project(B.waveCenter.x,B.waveCenter.y);const float Radius=B.waveRadius*WorldScale;
        const float Fade=FMath::Clamp(1.f-static_cast<float>(B.waveRadius/gw::BossWaveRadius),0.f,1.f);
        ArenaRing(P,Radius,FLinearColor(C.R,C.G,C.B,.13f+Fade*.2f),FMath::Max(3.f,18*WorldScale),64);
        ArenaRing(P,Radius,FLinearColor(C.R,C.G,C.B,.35f+Fade*.6f),2,64);
        ArenaRing(P,FMath::Max(0.f,Radius-8*WorldScale),FLinearColor(1,.87f,.6f,Fade*.5f),1,64);
    }
    for(const auto& Bullet:B.projectiles)if(Bullet.active) {
        const auto P=Project(Bullet.position.x,Bullet.position.y);const float R=FMath::Max(2.5f,static_cast<float>(Bullet.radius)*WorldScale);
        const FVector2D V(Bullet.velocity.x,Bullet.velocity.y),D=V.IsNearlyZero()?FVector2D(1,0):V.GetSafeNormal(),N(-D.Y,D.X);
        const auto Point=[&](float X,float Y){return P+D*(X*R)+N*(Y*R);};
        ArenaLine(Point(-3.1f,0),Point(-1.1f,0),FLinearColor(C.R,C.G,C.B,.18f),FMath::Max(1.2f,R*.65f));
        // One hue, with a pointed nose, narrow body and two tail fins aligned to velocity.
        ArenaPolygon({Point(-1.15f,-.36f),Point(.7f,-.36f),Point(.7f,.36f),Point(-1.15f,.36f)},C);
        ArenaPolygon({Point(.7f,-.36f),Point(1.75f,0),Point(.7f,.36f)},C);
        ArenaPolygon({Point(-.35f,-.3f),Point(-1.45f,-.94f),Point(-1.18f,-.3f)},C);
        ArenaPolygon({Point(-.35f,.3f),Point(-1.18f,.3f),Point(-1.45f,.94f)},C);
    }
    const float R=110*WorldScale,Jump=B.jumpHeight*WorldScale;
    if(Position.X+R<ArenaX||Position.X-R>ArenaX+ArenaSide||Position.Y+R<ArenaY||Position.Y-R-Jump>ArenaY+ArenaSide)return;
    ArenaRing(Position,R*.75f,FLinearColor(.15f,.08f,.02f,.15f),FMath::Max(3.f,R*.2f),24);
    const auto P=Position-FVector2D(0,Jump);
    const TArray<FVector2D> Shape={P+FVector2D(-.62f,-.8f)*R,P+FVector2D(.62f,-.8f)*R,P+FVector2D(1,.8f)*R,P+FVector2D(-1,.8f)*R};
    const float Hit=FMath::Clamp(static_cast<float>(B.hitFlash/.18),0.f,1.f);
    ArenaPolygon(Shape,FMath::Lerp(FMath::Lerp(FLinearColor(1,.96f,.88f,1),C,.3f),FLinearColor::White,Hit));
    for(int32 i=0;i<4;++i)ArenaLine(Shape[i],Shape[(i+1)%4],C,2.5f);
    const auto Eye=P+FVector2D(0,-R*.08f);
    ArenaLine(Eye+FVector2D(-R*.45f,-R*.08f),Eye+FVector2D(-R*.17f,R*.02f),C,FMath::Max(2.f,R*.07f));
    ArenaLine(Eye+FVector2D(R*.17f,R*.02f),Eye+FVector2D(R*.45f,-R*.08f),C,FMath::Max(2.f,R*.07f));
    if(InArena(P,R+15)&&WorldScale>.22f)Text(B.rage?TEXT("暴怒 · 梯形"):TEXT("几何体 | 梯形"),P.X,P.Y-R-19,11,C,true);
}
void AArenaHUD::DrawGunfire(AArenaGameMode* G) {
    const auto& M=G->GetMatch();
    for(size_t Index=0;Index<M.fighters.size();++Index) {
        const auto& F=M.fighters[Index];if(!F.alive || F.weaponKind!=gw::WeaponKind::Sniper || F.aimRemaining<=0 || F.targetKind==0 || F.reloadRemaining>0)continue;
        gw::Vec Target;const int32 I=F.targetIndex;
        if(F.targetKind==1 && I>=0 && I<static_cast<int32>(M.fighters.size()) && M.fighters[I].alive)Target=M.world.bodies[I].position;
        else if(F.targetKind==2 && I>=0 && I<static_cast<int32>(M.npcs.size()) && M.npcs[I].active)Target=M.npcs[I].position;
        else if(F.targetKind==3 && I>=1 && I<=2 && M.bases[I].alive)Target=M.bases[I].position;
        else if(F.targetKind==4 && M.boss.active)Target=M.boss.position;
        else if(F.targetKind==5 && I>=0 && I<static_cast<int32>(M.evolutionPacks.size()) && M.evolutionPacks[I].active)Target=M.evolutionPacks[I].position;
        else if(F.targetKind==6 && I>=0 && I<static_cast<int32>(M.weaponCrates.size()) && M.weaponCrates[I].active)Target=M.weaponCrates[I].position;
        else continue;
        const auto& Position=M.world.bodies[Index].position;const FVector2D D(FMath::Cos(F.aimAngle),FMath::Sin(F.aimAngle));
        const auto A=Project(Position.x,Position.y)+D*(76*WorldScale*M.world.bodies[Index].scale);
        const auto Z=Project(Position.x,Position.y)+D*((Target-Position).length()*WorldScale);
        const double AimDuration=F.sniperAimDuration>0?F.sniperAimDuration:M.weaponFor(F).aimTime;
        const float Progress=1-FMath::Clamp(static_cast<float>(F.aimRemaining/FMath::Max(AimDuration,.001)),0.f,1.f);
        ArenaLine(A,Z,FLinearColor(.95f,.035f,.025f,.28f+.35f*Progress),FMath::Max(.8f,1.2f*WorldScale));
    }
    for(const auto& Projectile:M.projectiles)if(Projectile.active) {
        const auto P=Project(Projectile.position.x,Projectile.position.y);
        const FVector2D Velocity(Projectile.velocity.x,Projectile.velocity.y),D=Velocity.GetSafeNormal(),N(-D.Y,D.X);
        const auto C=CombatTeam(Projectile.team);
        if(Projectile.kind==gw::WeaponKind::RocketLauncher) {
            const float R=FMath::Max(2.4f,static_cast<float>(Projectile.radius)*WorldScale);
            const auto Point=[&](float X,float Y){return P+D*(X*R)+N*(Y*R);};
            ArenaPolygon({Point(-1.5f,-.38f),Point(.7f,-.38f),Point(1.6f,0),Point(.7f,.38f),Point(-1.5f,.38f)},C);
            ArenaPolygon({Point(-.5f,-.33f),Point(-1.7f,-.85f),Point(-1.35f,-.25f)},C);
            ArenaPolygon({Point(-.5f,.33f),Point(-1.35f,.25f),Point(-1.7f,.85f)},C);
        } else {
            const float Length=FMath::Max(4.f,(Projectile.kind==gw::WeaponKind::Sniper?54.f:18.f)*WorldScale);
            ArenaLine(P-D*Length,P,C,FMath::Max(1.2f,static_cast<float>(Projectile.radius)*2*WorldScale));
        }
    }
    for(const auto& S:M.shots) {
        const auto A=Project(S.from.x,S.from.y),B=Project(S.to.x,S.to.y);const auto D=(B-A).GetSafeNormal();
        const float Progress=FMath::Clamp(1-static_cast<float>(S.life/FMath::Max(S.lifetime,.001)),0.f,1.f);
        const bool Shotgun=S.kind==gw::WeaponKind::Shotgun;
        const float Flight=FMath::Clamp(Progress/.7f,0.f,1.f);const auto P=FMath::Lerp(A,B,Flight);
        auto C=CombatTeam(S.team);const float Fade=1-Progress;
        C.A=Fade;
        const float Length=FMath::Min(static_cast<float>((B-A).Size())*Flight,FMath::Max(4.f,(Shotgun?22.f:46.f)*WorldScale));
        ArenaLine(P-D*Length,P,FLinearColor(C.R,C.G,C.B,Fade*.22f),Shotgun?4.f:5.f);
        ArenaLine(P-D*Length,P,C,Shotgun?1.5f:2.f);
        ArenaLine(P-D*FMath::Min(Length,3.f),P,FLinearColor(1,.91f,.57f,Fade),2.5f);
        if(Progress<.22f) {
            const FVector2D N(-D.Y,D.X);const float Flash=(1-Progress/.22f)*FMath::Max(4.f,18*WorldScale);
            ArenaPolygon({A,A+D*Flash+N*Flash*.36,A+D*Flash*1.7,A+D*Flash-N*Flash*.36},FLinearColor(1,.62f,.08f,.75f));
            ArenaLine(A-D*2,A+D*Flash,FLinearColor(1,.96f,.74f,1),2);
        }
        if(S.hit&&Progress>.6f) {
            const float Impact=(Progress-.6f)/.4f;const float R=FMath::Max(5.f,24*WorldScale)*Impact;
            for(int32 i=0;i<4;++i){const float Angle=i*PI/2+.4f;const FVector2D Ray(FMath::Cos(Angle),FMath::Sin(Angle));ArenaLine(B+Ray*R*.4,B+Ray*R,FLinearColor(C.R,C.G,C.B,1-Impact),1.5f);}
        }
    }
}
void AArenaHUD::DrawBossBar(AArenaGameMode* G) {
    const auto& M=G->GetMatch();const auto& B=M.boss;if(!B.active)return;
    const float Width=FMath::Clamp(ArenaSide-365.f,100.f,280.f),X=ArenaX+ArenaSide*.5f-Width*.5f,Y=111;
    const auto C=B.rage?CombatRed:CombatGold;
    Text(B.spawnAge<gw::BossSpawnWaveSeconds?TEXT("梯形 BOSS · 引力冲击"):B.rage?TEXT("梯形 BOSS · 暴怒"):TEXT("几何体 | 梯形"),X+Width/2,91,9,C,true);
    HealthBar(X,Y,Width,17,B.hp,B.maxHp,C,9);
    if(B.rage)for(int32 i=0;i<16;++i){const float Phase=FMath::Fmod(G->RunningTime*1.9f+i*.319f,1.f),Px=X+(i+.5f)*Width/16;const float Height=3+8*Phase;Line({Px,Y-1},{Px+2,Y-Height},FLinearColor(1,.2f,.015f,(1-Phase)*.85f),2);}
}
