using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Newtonsoft.Json.Linq;

namespace GeometricWarfare.DouyinHost
{
    public sealed class BackendFailure : Exception { public int Code { get; } public BackendFailure(int code) : base("Backend operation failed") { Code = code; } }
    public sealed class LiveBackendClient : IDisposable
    {
        const int MaxResponse = 2 * 1024 * 1024;
        readonly Uri endpoint;
        readonly HttpClient http;
        readonly Func<long> now;
        readonly SemaphoreSlim gate = new SemaphoreSlim(1, 1);
        readonly object sessionLock = new object();
        string sessionToken, sessionApp, sessionRoom, sessionAnchor;
        long expiresAt;
        bool authenticatedOnce;
        int generation;

        public static Uri ValidateEndpoint(string url, bool allowInsecure)
        {
            if (!Uri.TryCreate(url, UriKind.Absolute, out var uri) || string.IsNullOrWhiteSpace(uri.Host) ||
                !string.IsNullOrEmpty(uri.UserInfo) || url.IndexOf('?') >= 0 || url.IndexOf('#') >= 0 ||
                (uri.Scheme != Uri.UriSchemeHttps && !(allowInsecure && uri.Scheme == Uri.UriSchemeHttp &&
                (string.Equals(uri.Host, "localhost", StringComparison.OrdinalIgnoreCase) || uri.Host == "127.0.0.1"))))
                throw new ArgumentException("Invalid backend endpoint");
            return uri;
        }
        internal static HttpClientHandler CreateHandler() => new HttpClientHandler {
            AllowAutoRedirect = false, UseCookies = false, UseProxy = false
        }; // Certificate validation remains the framework default.
        public LiveBackendClient(string url, bool insecure) : this(url, insecure, CreateHandler(), () => DateTimeOffset.UtcNow.ToUnixTimeSeconds()) { }
        internal LiveBackendClient(string url, bool insecure, HttpMessageHandler handler, Func<long> now)
        {
            endpoint = ValidateEndpoint(url, insecure);
            http = new HttpClient(handler, true) { Timeout = Timeout.InfiniteTimeSpan };
            this.now = now;
        }
        public void ResetSession()
        {
            lock (sessionLock) {
                ++generation; sessionToken = sessionApp = sessionRoom = sessionAnchor = null;
                expiresAt = 0; authenticatedOnce = false;
            }
        }
        public async Task<JObject> AuthenticateAsync(string app, string room, string anchor, string launch, CancellationToken token)
        {
            int expected; lock (sessionLock) expected = generation;
            using (var deadline = CancellationTokenSource.CreateLinkedTokenSource(token)) {
                deadline.CancelAfter(15000);
                bool entered = false;
                try {
                    await gate.WaitAsync(deadline.Token).ConfigureAwait(false); entered = true;
                    lock (sessionLock) if (expected != generation) throw new BackendFailure(-1011);
                    await EnsureSession(app, room, anchor, launch, deadline.Token).ConfigureAwait(false);
                    lock (sessionLock) if (expected != generation) throw new BackendFailure(-1011);
                    return new JObject { ["app_id"] = app, ["room_id"] = room, ["anchor_open_id"] = anchor };
                }
                catch (BackendFailure) { throw; }
                catch (OperationCanceledException) { throw new BackendFailure(-1014); }
                catch { throw new BackendFailure(-1013); }
                finally { if (entered) gate.Release(); }
            }
        }
        public async Task<JObject> SubmitRoundAsync(string app, string room, string anchor, string launch, JObject round, CancellationToken token)
        {
            JObject payload = PrepareRound(app, room, round);
            int expected; lock (sessionLock) expected = generation;
            using (var deadline = CancellationTokenSource.CreateLinkedTokenSource(token)) {
                deadline.CancelAfter(15000);
                bool entered = false;
                try {
                    await gate.WaitAsync(deadline.Token).ConfigureAwait(false); entered = true;
                    lock (sessionLock) if (expected != generation) throw new BackendFailure(-1011);
                    await EnsureSession(app, room, anchor, launch, deadline.Token).ConfigureAwait(false);
                    string bearer;
                    lock (sessionLock) { if (expected != generation) throw new BackendFailure(-1011); bearer = sessionToken; }
                    var response = await Post("rounds", bearer, payload, deadline.Token).ConfigureAwait(false);
                    lock (sessionLock) if (expected != generation) throw new BackendFailure(-1011);
                    return ValidateReceipt(response, payload);
                }
                catch (BackendFailure error) {
                    if (error.Code == 401 || error.Code == 403) lock (sessionLock) if (authenticatedOnce) { sessionToken = null; expiresAt = 0; }
                    throw;
                }
                catch (OperationCanceledException) { throw new BackendFailure(-1014); }
                catch { throw new BackendFailure(-1013); }
                finally { if (entered) gate.Release(); }
            }
        }
        async Task EnsureSession(string app, string room, string anchor, string launch, CancellationToken token)
        {
            int expected;
            lock (sessionLock) {
                bool same = sessionApp == app && sessionRoom == room && sessionAnchor == anchor;
                if (same && sessionToken != null && expiresAt > now()) return;
                if (same && authenticatedOnce) throw new BackendFailure(-1015);
                expected = generation;
            }
            var response = await Post("sessions", launch, new JObject { ["app_id"] = app, ["room_id"] = room, ["anchor_open_id"] = anchor }, token).ConfigureAwait(false);
            string bearer = Text(response, "session_token");
            long expiry = Integer(response["expires_at"]);
            if (Text(response, "app_id") != app || Text(response, "room_id") != room || Text(response, "anchor_open_id") != anchor ||
                bearer.Length > 16384 || bearer.IndexOfAny(new[] { '\r', '\n' }) >= 0 || expiry <= now() || expiry > now() + 43260)
                throw new BackendFailure(-1013);
            lock (sessionLock) {
                if (expected != generation) throw new BackendFailure(-1011);
                sessionToken = bearer; sessionApp = app; sessionRoom = room; sessionAnchor = anchor;
                expiresAt = expiry; authenticatedOnce = true;
            }
        }
        async Task<JObject> Post(string action, string bearer, JObject payload, CancellationToken token)
        {
            var builder = new UriBuilder(endpoint) { Path = endpoint.AbsolutePath.TrimEnd('/') + "/v1/douyin/" + action };
            using (var request = new HttpRequestMessage(HttpMethod.Post, builder.Uri)) {
                request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", bearer);
                request.Content = new StringContent(payload.ToString(Newtonsoft.Json.Formatting.None), Encoding.UTF8, "application/json");
                using (var response = await http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, token).ConfigureAwait(false)) {
                    if (!response.IsSuccessStatusCode) throw new BackendFailure((int)response.StatusCode);
                    if (response.Content.Headers.ContentLength > MaxResponse) throw new BackendFailure(-1013);
                    using (var stream = await response.Content.ReadAsStreamAsync().ConfigureAwait(false))
                    using (var content = new MemoryStream()) {
                        var buffer = new byte[8192];
                        int read;
                        while ((read = await stream.ReadAsync(buffer, 0, buffer.Length, token).ConfigureAwait(false)) > 0) {
                            if (content.Length + read > MaxResponse) throw new BackendFailure(-1013);
                            content.Write(buffer, 0, read);
                        }
                        return JObject.Parse(new UTF8Encoding(false, true).GetString(content.ToArray()));
                    }
                }
            }
        }
        static JObject PrepareRound(string app, string room, JObject round)
        {
            var input = round["users"] as JArray;
            if (input == null || input.Count > 5000) throw new BackendFailure(-1001);
            long id = DecimalInteger(round["round_id"]), start = Integer(round["start_time"]), end = Integer(round["end_time"]);
            if (id <= 0 || start <= 0 || end < start) throw new BackendFailure(-1001);
            var users = new JArray(); var ids = new HashSet<string>(StringComparer.Ordinal);
            foreach (var user in input) {
                string open = Text(user, "open_id"), group = Text(user, "group_id");
                long score = DecimalInteger(user["score"]), result = Integer(user["result"]);
                if (!ids.Add(open) || score < 0 || result < 1 || result > 3) throw new BackendFailure(-1001);
                users.Add(new JObject { ["open_id"] = open, ["group_id"] = group, ["score"] = score.ToString(CultureInfo.InvariantCulture), ["result"] = result });
            }
            return new JObject { ["app_id"] = app, ["room_id"] = room, ["round_id"] = id.ToString(CultureInfo.InvariantCulture), ["start_time"] = start, ["end_time"] = end, ["users"] = users };
        }
        static JObject ValidateReceipt(JObject response, JObject submitted)
        {
            if (response["accepted"]?.Type != JTokenType.Boolean || !(bool)response["accepted"] ||
                Text(response, "round_id") != (string)submitted["round_id"]) throw new BackendFailure(-1013);
            string version = Text(response, "world_rank_version");
            var input = (JArray)submitted["users"]; var results = response["users"] as JArray;
            if (results == null || results.Count != input.Count) throw new BackendFailure(-1013);
            var expected = new HashSet<string>(StringComparer.Ordinal);
            foreach (var user in input) expected.Add((string)user["open_id"]);
            var enriched = new JArray();
            foreach (var user in results) {
                string open = Text(user, "open_id"); long points = DecimalInteger(user["win_points"]), streak = DecimalInteger(user["win_streak"]);
                if (!expected.Remove(open) || points < 0 || points > 1 || streak < 0 || streak > int.MaxValue) throw new BackendFailure(-1013);
                enriched.Add(new JObject { ["open_id"] = open, ["win_points"] = points.ToString(CultureInfo.InvariantCulture), ["win_streak"] = streak.ToString(CultureInfo.InvariantCulture) });
            }
            return new JObject { ["accepted"] = true, ["round_id"] = (string)submitted["round_id"], ["world_rank_version"] = version, ["users"] = enriched };
        }
        static string Text(JToken value, string field)
        {
            var token = value[field];
            if (token?.Type != JTokenType.String || string.IsNullOrWhiteSpace((string)token) || ((string)token).Length > 16384) throw new BackendFailure(-1013);
            return (string)token;
        }
        static long Integer(JToken token)
        {
            if (token?.Type != JTokenType.Integer) throw new BackendFailure(-1013);
            return (long)token;
        }
        static long DecimalInteger(JToken token)
        {
            if (token?.Type == JTokenType.Integer) return (long)token;
            if (token?.Type != JTokenType.String || !long.TryParse((string)token, NumberStyles.None, CultureInfo.InvariantCulture, out long value)) throw new BackendFailure(-1013);
            return value;
        }
        public void Dispose() { ResetSession(); http.Dispose(); }
    }
}
