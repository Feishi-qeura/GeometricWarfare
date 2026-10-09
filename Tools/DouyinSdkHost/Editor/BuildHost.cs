using System;
using System.IO;
using GeometricWarfare.DouyinHost;
using ByteDance.LiveOpenSdk.Push;
using ByteDance.LiveOpenSdk.Room;
using Newtonsoft.Json.Linq;
using UnityEditor;
using UnityEditor.Build.Reporting;
using UnityEditor.SceneManagement;
using UnityEngine;

public static class BuildHost
{
    public static void Build()
    {
        VerifyProtocolContracts();
        HostSchedulingTests.Run();
        string output = Environment.GetEnvironmentVariable("GWSDK_BUILD_OUTPUT");
        if (string.IsNullOrEmpty(output)) throw new InvalidOperationException("GWSDK_BUILD_OUTPUT required");
        Directory.CreateDirectory(output);
        var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        new GameObject("DouyinSdkHost").AddComponent<DouyinSdkHost>();
        EditorSceneManager.SaveScene(scene, "Assets/DouyinSdkHost.unity");
        PlayerSettings.productName = "GeometricWarfare Douyin SDK Host";
        PlayerSettings.companyName = "FEISHI";
        PlayerSettings.SetScriptingBackend(BuildTargetGroup.Standalone, ScriptingImplementation.Mono2x);
        PlayerSettings.SetApiCompatibilityLevel(BuildTargetGroup.Standalone, ApiCompatibilityLevel.NET_Standard_2_0);
        PlayerSettings.runInBackground = true;
        var report = BuildPipeline.BuildPlayer(new BuildPlayerOptions {
            scenes = new[] { "Assets/DouyinSdkHost.unity" }, locationPathName = Path.Combine(output, "DouyinSdkHost.exe"),
            target = BuildTarget.StandaloneWindows64, options = BuildOptions.None });
        if (report.summary.result != BuildResult.Succeeded) throw new InvalidOperationException("SDK host build failed");
    }

    public static void VerifyProtocolContracts()
    {
        var like = DouyinSdkHost.EncodeMessage(new LikeFixture { MsgType = "live_like" }, "123");
        Require(like["count"].Type == JTokenType.String && (string)like["count"] == "9223372036854775807", "like count Int64");
        var gift = DouyinSdkHost.EncodeMessage(new GiftFixture { MsgType = "live_gift" }, "123");
        Require(gift["count"].Type == JTokenType.String && (string)gift["count"] == "9007199254740993", "gift count above double precision");
        Require((string)gift["gift_id"] == "gift" && (string)gift["audience_open_id"] == "" && (bool)gift["is_test"], "gift fields");
        var follow = DouyinSdkHost.EncodeMessage(new FollowFixture { MsgType = "live_follow" }, "123");
        Require(follow["follow_target"].Type == JTokenType.String && (string)follow["follow_target"] == "anchor" && (int)follow["follow_action"] == 3, "follow field types");
        var presence = DouyinSdkHost.EncodeMessage(new PresenceFixture { MsgType = "live_enter" }, "123");
        Require((int)presence["enter_type"] == 2 && (int)presence["follow_status"] == 3 && (int)presence["enter_scene"] == 2 && (string)presence["inviter_id"] == "inviter", "presence field mapping");
        Require((string)presence["room_id"] == "123" && presence["timestamp"].Type == JTokenType.Integer && (string)presence["user"]["open_id"] == "viewer", "source envelope");
        File.WriteAllText("protocol-contract-checks.txt", "PASS: exact Int64 count strings, SDK gift/follow/presence fields, room/source envelope\n");
    }
    static void Require(bool value, string name) { if (!value) throw new InvalidOperationException("Protocol contract: " + name); }
    class BaseFixture : IPushMessage { public string MsgId => "fixture"; public string MsgType { get; set; } public long Timestamp => 1728000000000; }
    sealed class UserFixture : IUserInfo { public string OpenId => "anchor"; public string AvatarUrl => ""; public string Nickname => "主播"; }
    sealed class LikeFixture : BaseFixture, ILikeMessage { public IUserInfo Sender => new UserFixture(); public long LikeCount => long.MaxValue; }
    sealed class GiftFixture : BaseFixture, IGiftMessage {
        public IUserInfo Sender => new UserFixture(); public string SecGiftId => "gift"; public string SecMagicGiftId => "";
        public long GiftCount => 9007199254740993; public long GiftValue => 1; public bool IsTestData => true; public string AudienceSecOpenId => "";
    }
    sealed class FollowFixture : BaseFixture, IFollowMessage { public IUserInfo Sender => new UserFixture(); public IUserInfo FollowUser => new UserFixture(); public FollowAction UserFollowAction => FollowAction.FollowBack; }
    sealed class PresenceFixture : BaseFixture, IEnterRoomMessage {
        public string SecOpenId => "viewer"; public string AvatarUrl => ""; public string NickName => "观众"; public long IsOldPlayer => 0;
        public string GradeLevel => ""; public string InviterGatherOpenid => "inviter"; public long EnterRoomType => 2;
        public string InviterGatherNickname => "邀请人"; public string InviterGatherAvatarUrl => ""; public int EnterRoomScene => 2; public long FollowStatus => 3;
    }
}
