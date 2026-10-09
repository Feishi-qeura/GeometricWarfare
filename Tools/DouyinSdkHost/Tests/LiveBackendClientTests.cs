using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Http;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using GeometricWarfare.DouyinHost;
using Newtonsoft.Json.Linq;

static class LiveBackendClientTests
{
    const string App = "ttFixture", Room = "7214015683695250235", Anchor = "fixture-anchor";
    const string Launch = "FAKE_LAUNCH_NOT_REAL", Session = "FAKE_SESSION_NOT_REAL";
    static int Main()
    {
        try { Run().GetAwaiter().GetResult(); Console.WriteLine("PASS: backend HTTP contract and loopback wire, early authentication, 5000 users, base path, generation reset, session reuse/expiry, exact Int64, bounded responses, cancellation and sanitized errors"); return 0; }
        catch (Exception error) { Console.Error.WriteLine("FAIL: " + error.GetType().Name + " code=" + (error as BackendFailure)?.Code + " " + error.StackTrace); return 1; }
    }
    static void Check(bool ok) { if (!ok) throw new InvalidOperationException(); }
    static JObject Round() => JObject.Parse("{\"round_id\":\"9007199254740993\",\"start_time\":1728000000,\"end_time\":1728000420,\"users\":[{\"open_id\":\"viewer\",\"group_id\":\"red\",\"score\":\"9223372036854775807\",\"result\":1}]}");
    static HttpResponseMessage Json(string body, HttpStatusCode status = HttpStatusCode.OK) => new HttpResponseMessage(status) { Content = new StringContent(body, Encoding.UTF8, "application/json") };
    static HttpResponseMessage SessionReply() => Json(new JObject { ["session_token"] = Session, ["expires_at"] = 1728003600, ["app_id"] = App, ["room_id"] = Room, ["anchor_open_id"] = Anchor }.ToString());
    sealed class UnknownLengthContent : HttpContent
    {
        readonly byte[] content = new byte[2 * 1024 * 1024 + 1];
        protected override bool TryComputeLength(out long length) { length = 0; return false; }
        protected override Task SerializeToStreamAsync(Stream stream, TransportContext context) => stream.WriteAsync(content, 0, content.Length);
        protected override Task<Stream> CreateContentReadStreamAsync() => Task.FromResult<Stream>(new MemoryStream(content, false));
    }
    sealed class Handler : HttpMessageHandler
    {
        internal readonly List<string> Paths = new List<string>();
        internal Func<HttpRequestMessage, Task<HttpResponseMessage>> Reply;
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken token)
        { Paths.Add(request.RequestUri.AbsolutePath); return Reply(request); }
    }
    static async Task Run()
    {
        Check(LiveBackendClient.ValidateEndpoint("https://example.invalid", false).Scheme == "https");
        foreach (string bad in new[] { "http://example.invalid", "http://127.0.0.2", "https://u:p@example.invalid", "https://example.invalid?q=1", "https://example.invalid/#x", "file:///C:/test", "http://[::1]" })
        {
            bool rejected = false; try { LiveBackendClient.ValidateEndpoint(bad, true); } catch (ArgumentException) { rejected = true; } Check(rejected);
        }
        bool disabled = false; try { LiveBackendClient.ValidateEndpoint("http://localhost:19090", false); } catch (ArgumentException) { disabled = true; } Check(disabled);
        Check(LiveBackendClient.ValidateEndpoint("http://127.0.0.1:19090", true).IsLoopback);
        using (var configured = LiveBackendClient.CreateHandler()) Check(!configured.AllowAutoRedirect && !configured.UseCookies && !configured.UseProxy);
        long now = 1728000000;
        var mock = new Handler();
        mock.Reply = async request => {
            string body = await request.Content.ReadAsStringAsync();
            Check(request.Headers.Authorization.Scheme == "Bearer");
            var payload = JObject.Parse(body);
            if (request.RequestUri.AbsolutePath.EndsWith("/sessions")) {
                Check(request.Headers.Authorization.Parameter == Launch && (string)payload["anchor_open_id"] == Anchor);
                return Json("{\"session_token\":\"" + Session + "\",\"expires_at\":1728003600,\"app_id\":\"" + App + "\",\"room_id\":\"" + Room + "\",\"anchor_open_id\":\"" + Anchor + "\"}");
            }
            Check(request.Headers.Authorization.Parameter == Session && !body.Contains(Launch) && (string)payload["users"][0]["score"] == "9223372036854775807");
            return Json("{\"accepted\":true,\"round_id\":\"9007199254740993\",\"world_rank_version\":\"fixture-week\",\"session_token\":\"MUST_NOT_FORWARD\",\"users\":[{\"open_id\":\"viewer\",\"win_points\":1,\"win_streak\":2}]}");
        };
        using (var client = new LiveBackendClient("https://example.invalid", false, mock, () => now)) {
            var first = await client.SubmitRoundAsync(App, Room, Anchor, Launch, Round(), CancellationToken.None);
            Check((bool)first["accepted"] && first["session_token"] == null && (string)first["users"][0]["win_points"] == "1");
            await client.SubmitRoundAsync(App, Room, Anchor, Launch, Round(), CancellationToken.None);
            Check(mock.Paths.Count == 3 && mock.Paths[0].EndsWith("/sessions"));
            now = 1728003600;
            await ExpectFailure(() => client.SubmitRoundAsync(App, Room, Anchor, Launch, Round(), CancellationToken.None), -1015);
            Check(mock.Paths.Count == 3); // Expiry must not quietly reuse the launch token.
        }
        await FailureCase(request => Json("{\"session_token\":\"" + Session + "\",\"expires_at\":1728003600,\"app_id\":\"" + App + "\",\"room_id\":\"wrong\",\"anchor_open_id\":\"" + Anchor + "\"}"), -1013);
        await FailureCase(request => Json("SECRET_RAW_BODY_" + Launch, HttpStatusCode.Unauthorized), 401);
        await FailureCase(request => { var r = Json("", HttpStatusCode.TemporaryRedirect); r.Headers.Location = new Uri("https://example.invalid/leak"); return r; }, 307);
        await FailureCase(request => Json(new string('x', 2 * 1024 * 1024 + 1)), -1013);
        await FailureCase(request => new HttpResponseMessage(HttpStatusCode.OK) { Content = new UnknownLengthContent() }, -1013);
        await FailureCase(request => Json("not-json-" + Launch), -1013);
        await LargeRoundAndEarlyAuth();
        await ResetWhileQueued();
        await ResetDuringRound();
        await LoopbackWire();
        var canceledHandler = new Handler { Reply = request => Task.FromResult(SessionReply()) };
        using (var client = new LiveBackendClient("https://example.invalid", false, canceledHandler, () => 1728000000))
        using (var canceled = new CancellationTokenSource()) {
            canceled.Cancel();
            await ExpectFailure(() => client.AuthenticateAsync(App, Room, Anchor, Launch, canceled.Token), -1014);
            Check(canceledHandler.Paths.Count == 0);
        }
    }
    static async Task LoopbackWire()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        var server = Task.Run(async () => {
            for (int i = 0; i < 2; ++i) using (var connection = await listener.AcceptTcpClientAsync())
            using (var stream = connection.GetStream())
            using (var reader = new StreamReader(stream, Encoding.UTF8, false, 1024, true)) {
                string requestLine = await reader.ReadLineAsync();
                Check(requestLine.StartsWith("POST /live-api/v1/douyin/" + (i == 0 ? "sessions" : "rounds") + " HTTP/"));
                int length = 0; string authorization = null, header;
                while (!string.IsNullOrEmpty(header = await reader.ReadLineAsync())) {
                    if (header.StartsWith("Content-Length:", StringComparison.OrdinalIgnoreCase)) length = int.Parse(header.Substring(15).Trim());
                    if (header.StartsWith("Authorization:", StringComparison.OrdinalIgnoreCase)) authorization = header.Substring(14).Trim();
                }
                Check(length > 0 && length < 4 * 1024 * 1024 && authorization == "Bearer " + (i == 0 ? Launch : Session));
                var body = new char[length]; int total = 0;
                while (total < length) { int read = await reader.ReadAsync(body, total, length - total); Check(read > 0); total += read; }
                var payload = JObject.Parse(new string(body));
                Check((string)payload["app_id"] == App && (string)payload["room_id"] == Room);
                string response;
                if (i == 0) {
                    Check((string)payload["anchor_open_id"] == Anchor);
                    response = new JObject { ["session_token"] = Session, ["expires_at"] = DateTimeOffset.UtcNow.ToUnixTimeSeconds() + 3600, ["app_id"] = App, ["room_id"] = Room, ["anchor_open_id"] = Anchor }.ToString();
                }
                else { Check((string)payload["users"][0]["score"] == "9223372036854775807"); response = "{\"accepted\":true,\"round_id\":\"9007199254740993\",\"world_rank_version\":\"fixture-week\",\"users\":[{\"open_id\":\"viewer\",\"win_points\":1,\"win_streak\":2}]}"; }
                byte[] bytes = Encoding.UTF8.GetBytes(response);
                byte[] headers = Encoding.ASCII.GetBytes("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + bytes.Length + "\r\nConnection: close\r\n\r\n");
                await stream.WriteAsync(headers, 0, headers.Length); await stream.WriteAsync(bytes, 0, bytes.Length); await stream.FlushAsync();
            }
        });
        try {
            using (var client = new LiveBackendClient("http://127.0.0.1:" + port + "/live-api", true)) {
                await client.AuthenticateAsync(App, Room, Anchor, Launch, CancellationToken.None);
                var result = await client.SubmitRoundAsync(App, Room, Anchor, Launch, Round(), CancellationToken.None);
                Check((bool)result["accepted"] && (string)result["users"][0]["win_streak"] == "2");
            }
        }
        finally { listener.Stop(); try { await server; } catch (SocketException) { } catch (ObjectDisposedException) { } }
    }
    static async Task LargeRoundAndEarlyAuth()
    {
        var round = Round(); var users = new JArray();
        for (int i = 0; i < 5000; ++i) users.Add(new JObject {
            ["open_id"] = i.ToString("D5") + new string('x', 123), ["group_id"] = "blue", ["score"] = "9223372036854775807", ["result"] = 1, ["nickname"] = new string('n', 64) });
        round["users"] = users;
        Check(Encoding.UTF8.GetByteCount(round.ToString(Newtonsoft.Json.Formatting.None)) > 1024 * 1024);
        var handler = new Handler { Reply = async request => {
            if (request.RequestUri.AbsolutePath.EndsWith("/sessions")) { Check(request.Headers.Authorization.Parameter == Launch); return SessionReply(); }
            Check(request.Headers.Authorization.Parameter == Session);
            string body = await request.Content.ReadAsStringAsync();
            Check(Encoding.UTF8.GetByteCount(body) < 4 * 1024 * 1024 && !body.Contains("nickname"));
            var uploaded = JObject.Parse(body); Check(((JArray)uploaded["users"]).Count == 5000);
            var results = new JArray(((JArray)uploaded["users"]).Select(u => new JObject {
                ["open_id"] = (string)u["open_id"], ["win_points"] = 1, ["win_streak"] = 2 }));
            return Json(new JObject { ["accepted"] = true, ["round_id"] = (string)uploaded["round_id"], ["world_rank_version"] = "fixture-week", ["users"] = results }.ToString());
        } };
        using (var client = new LiveBackendClient("https://example.invalid/live-api/", false, handler, () => 1728000000)) {
            var auth = await client.AuthenticateAsync(App, Room, Anchor, Launch, CancellationToken.None);
            Check(auth["session_token"] == null && handler.Paths.Count == 1 && handler.Paths[0] == "/live-api/v1/douyin/sessions");
            var result = await client.SubmitRoundAsync(App, Room, Anchor, Launch, round, CancellationToken.None);
            Check(((JArray)result["users"]).Count == 5000 && handler.Paths.Count == 2 && handler.Paths[1] == "/live-api/v1/douyin/rounds");
        }
    }
    static async Task ResetWhileQueued()
    {
        var release = new TaskCompletionSource<HttpResponseMessage>();
        var handler = new Handler { Reply = request => release.Task };
        using (var client = new LiveBackendClient("https://example.invalid", false, handler, () => 1728000000)) {
            var auth = client.AuthenticateAsync(App, Room, Anchor, Launch, CancellationToken.None);
            Check(handler.Paths.Count == 1);
            var waitingRound = client.SubmitRoundAsync(App, Room, Anchor, Launch, Round(), CancellationToken.None);
            client.ResetSession(); release.SetResult(SessionReply());
            await ExpectFailure(() => auth, -1011);
            await ExpectFailure(() => waitingRound, -1011);
            Check(handler.Paths.Count == 1); // A queued old room must not start a new exchange.
        }
    }
    static async Task ResetDuringRound()
    {
        var release = new TaskCompletionSource<HttpResponseMessage>();
        var handler = new Handler { Reply = request => request.RequestUri.AbsolutePath.EndsWith("/sessions") ? Task.FromResult(SessionReply()) : release.Task };
        using (var client = new LiveBackendClient("https://example.invalid", false, handler, () => 1728000000)) {
            await client.AuthenticateAsync(App, Room, Anchor, Launch, CancellationToken.None);
            var inFlight = client.SubmitRoundAsync(App, Room, Anchor, Launch, Round(), CancellationToken.None);
            Check(handler.Paths.Count == 2);
            client.ResetSession();
            release.SetResult(Json("{\"accepted\":true,\"round_id\":\"9007199254740993\",\"world_rank_version\":\"old\",\"users\":[{\"open_id\":\"viewer\",\"win_points\":1,\"win_streak\":2}]}"));
            await ExpectFailure(() => inFlight, -1011);
        }
    }
    static async Task FailureCase(Func<HttpRequestMessage, HttpResponseMessage> reply, int code)
    {
        var handler = new Handler { Reply = request => Task.FromResult(reply(request)) };
        using (var client = new LiveBackendClient("https://example.invalid", false, handler, () => 1728000000)) {
            await ExpectFailure(() => client.SubmitRoundAsync(App, Room, Anchor, Launch, Round(), CancellationToken.None), code);
            Check(handler.Paths.Count == 1);
        }
    }
    static async Task ExpectFailure(Func<Task<JObject>> action, int code)
    {
        bool failed = false;
        try { await action(); } catch (BackendFailure error) { Check(error.Code == code && !error.Message.Contains(Launch) && !error.Message.Contains(Session)); failed = true; }
        Check(failed);
    }
}
