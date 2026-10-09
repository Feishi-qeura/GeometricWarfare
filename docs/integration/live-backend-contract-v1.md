# Live backend v1 integration contract

Status: implementation contract for the user-authorized world leaderboard backend. Hosting remains portable; existing Tencent Cloud is recommended subject to server inspection. Do not deploy or insert credentials into the client.

## Confirmed scoring policy

Asia/Hong_Kong week boundaries are Sunday 23:00. Each half-open week accumulates final individual scores from valid red/blue participant results. World win_points equals the count of wins in that week; a win adds one and a loss adds zero. Consecutive wins increment win_streak, a loss resets it, and the new week resets statistics. Gray participants do not affect any world metric. The single-round win_points sent to the SDK is one for a win and zero for a loss/gray. Current games have a determined red/blue winner.

The user confirmed that the first successful server receipt determines the statistics week: a completed round arriving across cutoff counts in the new receiving week so it is not omitted. A late marker is retained for audit while its competitive score/win metrics are accumulated and queued normally. Within that receiving week, round order is (end_time, room_id, round_id) for deterministic cross-room/late-arrival handling. Cross-week duplicates return the original stored receipt and original period without new credit; a different submitted payload for the same app/room/round is a conflict. Derived reception week is not part of submitted-content equivalence. All data is separated by application and test/online environment. Do not accept an online environment override from a client request.

## Session exchange

`POST /v1/douyin/sessions`

- `Authorization: Bearer <platform launch token>`; never log headers or the credential.
- Body: `{app_id:string, room_id:string, anchor_open_id:string}` using the initialized SDK session identity.
- Backend uses its own AppSecret/application access token to obtain official live-room information from the launch token. It must verify returned room_id and nonempty anchor identity against both requested fields. It does not trust client-supplied identity as authentication.
- Response: `{app_id:string,room_id:string,anchor_open_id:string,session_token:string,expires_at:integer}`. The SDK host checks all three identity fields against its current session. The random backend bearer is bound to app/room/anchor/environment, stored only as a cryptographic digest in the database, and never put into a URL or persisted in a UE outbox.
- Fixed lifetime is configurable, initially 12 hours; this is our own service policy, not a claimed Douyin token lifetime. Expired sessions require a new valid platform authorization. No invented platform refresh endpoint.

## Durable round receipt

`POST /v1/douyin/rounds`

- Authorization uses the backend bearer from session exchange.
- Body: `{app_id,room_id,round_id,start_time,end_time,users:[{open_id,group_id,score,result}]}`. round_id and score are decimal integer strings; timestamps are Unix seconds. score is a nonnegative Int64. result is 1 win, 2 loss, 3 gray/tie; red/blue must agree with the actual round winner. Require unique user IDs and maximum 5000 users. Bounded request body, no arbitrary URLs.
- Source app/room must match the authenticated backend session. Group IDs come from server configuration Red/Blue/Grey.
- Response: `{accepted:true,round_id:string,world_rank_version:string,users:[{open_id,win_points,win_streak}]}`. Accepted means the archive, weekly aggregate and upload work were committed transactionally, not that Douyin accepted its world leaderboard yet. All real participants are returned; gray metrics are zero. Single-round win_points is 1/0; win_streak is the computed weekly consecutive-win count at this round.
- Duplicate requests return a consistent receipt without double accumulation. Conflicting content returns HTTP 409. Unsupported/invalid input is 4xx, upstream or transient storage failure is retriable 5xx/503. No raw upstream bodies or credentials in error responses.
- Future timestamps within the tolerated clock skew that cross a not-yet-reached week boundary return retriable HTTP 503 `round_time_pending` until the server reaches that boundary; no future active version is queued early.

## Client integration

Game reporter emits a platform-neutral `backend_round` operation with the complete frozen snapshot before SDK user-results batches. The provider's SDK host validates app/room and adds its private backend bearer; credentials are never visible to gameplay. Typed result data accompanies the existing correlated command result so the reporter can enrich frozen user_results and room_rank metrics before submitting them.

Endpoint configuration is empty until explicitly set to the intended server. Production accepts HTTPS only; loopback HTTP is permitted only behind an explicit non-Shipping development flag. A required backend that is missing or unreachable must preserve the snapshot and show a pending/failure state, not claim world success. Durable client outbox must contain no credential and must only replay into the same authenticated app/room. A successful backend receipt allows SDK single-round reporting even while the backend asynchronously retries world uploads.

## Backend upload worker

Maintain a transactional archive, weekly totals and ordered durable upload jobs. Serialize world-version uploads so stale jobs cannot overwrite newer rankings. Changed users are uploaded in batches at most 50; Top150 is explicitly sorted and uploaded separately. At weekly rollover, drain the final old-version snapshot and complete the old version, then switch active version. Retry transient failures with bounded exponential backoff; retain terminal errors for operator diagnosis instead of marking success. Exactly-once local accounting plus idempotent absolute upstream writes handles process restarts.

The concrete four OpenAPI wire payloads, headers and timestamp units follow the verified official contract fixtures, not guesses from this internal transport document. Debug data uses is_online_version=false.
