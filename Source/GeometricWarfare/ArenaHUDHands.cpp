#include "ArenaHUD.h"
#include "Simulation/ArenaMatch.h"

void AArenaHUD::DrawFighterWeapons(const gw::Fighter& F,FVector2D P,float Scale,const gw::Match& M) {
    const bool Dual=F.temporaryWeaponRemaining>0;
    for(int32 Hand=0;Hand<(Dual?2:1);++Hand) {
        const bool Right=Hand==1;
        const auto Kind=Right?F.temporaryWeaponKind:F.weaponKind;
        const double Angle=Right?F.rightWeapon.aimAngle:F.aimAngle;
        const double Shot=Right?F.rightWeapon.shotRemaining:F.shotRemaining;
        const FVector2D Aim(FMath::Cos(Angle),FMath::Sin(Angle)),Side(-Aim.Y,Aim.X);
        const auto Origin=P+Side*(Dual?(Right?10.f:-10.f):0.f)*Scale;
        const FLinearColor Ink=Right?FLinearColor(.72f,.39f,.025f,1):FLinearColor(.035f,.065f,.08f,1);
        const float Recoil=FMath::Max(0.,Shot-(M.weaponFor(F,Right).fireInterval-.10))*45;
        const float Barrel=Kind==gw::WeaponKind::Pistol?44.f:Kind==gw::WeaponKind::Sniper?76.f:Kind==gw::WeaponKind::MachineGun?65.f:56.f;
        ArenaLine(Origin+Aim*(17-Recoil)*Scale,Origin+Aim*(Barrel-Recoil)*Scale,Ink,FMath::Max(2.f,(Kind==gw::WeaponKind::RocketLauncher?12.f:5.f)*WorldScale));
        if(Kind==gw::WeaponKind::Shotgun)ArenaLine(Origin+Aim*20*Scale+Side*4*Scale,Origin+Aim*Barrel*Scale+Side*4*Scale,Ink,FMath::Max(1.f,2*WorldScale));
        if(Kind==gw::WeaponKind::Rifle)ArenaLine(Origin+Aim*28*Scale,Origin+Aim*23*Scale+Side*9*Scale,Ink,FMath::Max(2.f,4*WorldScale));
        if(Kind==gw::WeaponKind::Sniper){ArenaLine(Origin+Aim*32*Scale-Side*7*Scale,Origin+Aim*48*Scale-Side*7*Scale,Ink,FMath::Max(2.f,5*WorldScale));ArenaLine(Origin+Aim*68*Scale-Side*4*Scale,Origin+Aim*68*Scale+Side*4*Scale,Ink,2);}
        if(Kind==gw::WeaponKind::MachineGun){ArenaLine(Origin+Aim*21*Scale+Side*8*Scale,Origin+Aim*46*Scale+Side*8*Scale,Ink,FMath::Max(2.f,7*WorldScale));ArenaLine(Origin+Aim*52*Scale-Side*4*Scale,Origin+Aim*Barrel*Scale-Side*4*Scale,Ink,FMath::Max(1.f,2*WorldScale));}
        if(Kind==gw::WeaponKind::RocketLauncher)ArenaLine(Origin+Aim*Barrel*Scale-Side*9*Scale,Origin+Aim*Barrel*Scale+Side*9*Scale,Ink,FMath::Max(2.f,4*WorldScale));
    }
}

void AArenaHUD::DrawHealingEffect(const gw::Fighter& F,FVector2D P,float Scale) {
    if(!F.alive || F.healFlash<=0)return;
    const float Life=FMath::Clamp(static_cast<float>(F.healFlash/.7),0.f,1.f),Age=1-Life;
    const float Pixel=FMath::Max(ScreenLayout.scale,.01f);
    const float R=FMath::Max(30*Scale,5.f/Pixel)+Age*FMath::Max(24*Scale,7.f/Pixel);
    const FLinearColor Green(.035f,.70f,.24f,Life*.85f);
    ArenaRing(P,R,Green,FMath::Max(1.4f,2*Scale),16);
    for(int32 I=0;I<3;++I) {
        const float Unit=FMath::Max(3.f*Scale,2.f/Pixel);
        const auto C=P+FVector2D((I-1)*Unit*4,-R*.5f-Age*Unit*9-(I%2)*Unit*3);
        ArenaLine(C-FVector2D(Unit,0),C+FVector2D(Unit,0),Green,FMath::Max(1.4f,Unit*.65f));
        ArenaLine(C-FVector2D(0,Unit),C+FVector2D(0,Unit),Green,FMath::Max(1.4f,Unit*.65f));
    }
}
