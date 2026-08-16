//! CEC-Dongle — a REST-controlled HDMI-CEC bridge (SMLIGHT SLWF-08 hardware, standalone Arduino
//! firmware). Project: <https://github.com/The1TrueJoe/CEC-Dongle>
//!
//! ```text
//!   POST /api/cec/cmd?name=<name>   run a named command — tv_on, tv_off, volume_up, input2, ...
//!   GET  /api/status                version, hostname, wifi status — used to confirm the device
//!   TCP  :9000                      one JSON object per line, pushed the moment the CEC bus
//!                                   reports a change (see firmware's CecPushServer)
//! ```
//!
//! # Commands, not opcodes
//!
//! The dongle's REST API already resolves CEC logical addresses and opcodes from its own saved
//! config (`GET /api/cec/commands` lists every name it accepts) — this driver only has to name
//! the command, never build a CEC frame itself. That split is deliberate on the firmware side:
//! all CEC tuning happens on the dongle's own web UI, so this driver and the dongle's Control4
//! driver both stay this thin.
//!
//! # No absolute volume, no held keys, no EDID
//!
//! CEC itself has no "set volume to N" opcode, only step up/down — so `has_discrete_volume` is
//! not declared and `set_volume` is refused rather than silently doing nothing useful. Likewise
//! there is no way to hold a CEC key down and have the TV ramp; the dongle sends a keypress and
//! its release, not a stream, so `has_hold` is not declared either. And the SLWF-08 has no
//! EDID/DDC line at all — it cannot ask a TV how many HDMI ports it has, so the manifest's four
//! `[[connection]]` entries are a guess, the same limitation the project's own docs describe.

use driver_sdk::*;
use driver_sdk::Value;

#[derive(Default)]
pub struct CecDongle;

const TV: LocalId = 1;

impl CecDongle {
    fn base(inst: &Instance) -> Option<String> {
        let addr = inst.property("Address").as_str()?.trim().to_string();
        if addr.is_empty() {
            return None;
        }
        Some(format!("http://{addr}"))
    }

    /// A named command — see `GET /api/cec/commands` on the dongle for the full list this can
    /// name. Destinations and opcodes are the dongle's problem, not this driver's.
    fn cmd(inst: &Instance, name: &str) -> Option<HostCall> {
        Some(HostCall::Http(HttpRequest::new(
            "POST",
            format!("{}/api/cec/cmd?name={name}", Self::base(inst)?),
        )))
    }

    /// `set_input`'s connection id -> the dongle's own `inputN` command name. The inverse of
    /// this lives in [`Self::input_command_name`]'s counterpart in `on_event`, which maps the
    /// pushed `active_input` number back the other way.
    fn input_command(connection: u64) -> Option<String> {
        (1001..=1008).contains(&connection).then(|| format!("input{}", connection - 1000))
    }
}

impl DriverModule for CecDongle {
    fn on_command(
        &self,
        inst: &mut Instance,
        proxy: LocalId,
        cmd: &str,
        args: &Args,
    ) -> Vec<HostCall> {
        if Self::base(inst).is_none() {
            return vec![HostCall::warn("cec-dongle: set the Address on this device first")];
        }

        if (proxy, cmd) == (TV, "set_input") {
            let Some(connection) = args.get("connection").and_then(Value::as_u64) else {
                return vec![HostCall::warn("cec-dongle: set_input needs a connection")];
            };
            let Some(name) = Self::input_command(connection) else {
                return vec![HostCall::warn(format!("cec-dongle: no such connection {connection}"))];
            };
            let mut a = Args::new();
            a.insert("connection".into(), json!(connection));
            return vec![
                Self::cmd(inst, &name).expect("Address checked above"),
                HostCall::notify(TV, "input_changed", a),
            ];
        }

        if (proxy, cmd) == (TV, "set_mute") {
            let Some(mute) = args.get("mute").and_then(Value::as_bool) else {
                return vec![HostCall::warn("cec-dongle: set_mute needs a mute value")];
            };
            let name = if mute { "mute_on" } else { "mute_off" };
            let mut a = Args::new();
            a.insert("mute".into(), json!(mute));
            return vec![
                Self::cmd(inst, name).expect("Address checked above"),
                HostCall::notify(TV, "mute_changed", a),
            ];
        }

        let name = match (proxy, cmd) {
            (TV, "on") => "tv_on",
            (TV, "off") => "tv_off",
            (TV, "power_toggle") => "power_toggle",
            (TV, "volume_up") => "volume_up",
            (TV, "volume_down") => "volume_down",
            (TV, "mute_toggle") => "mute",

            // Declared without has_discrete_volume/has_hold/has_channel_control — see the
            // module doc — so core should not normally send these, but a refusal that says why
            // is worth more than a silent no-op if something does.
            (TV, "set_volume") => {
                return vec![HostCall::warn(
                    "cec-dongle: this TV has no absolute volume over CEC, only volume_up/volume_down",
                )];
            }
            (TV, "pulse_input") => {
                return vec![HostCall::warn(
                    "cec-dongle: this TV supports set_input directly; pulse_input is not needed",
                )];
            }
            (TV, "hold") | (TV, "release") => {
                return vec![HostCall::warn(
                    "cec-dongle: CEC has no held key here, only a step — see volume_up/volume_down",
                )];
            }
            (TV, "set_channel") => {
                return vec![HostCall::warn("cec-dongle: this bridges CEC only, not a tuner")];
            }

            (_, other) => return vec![HostCall::warn(format!("cec-dongle: unhandled `{other}`"))],
        };

        let mut out = vec![Self::cmd(inst, name).expect("Address checked above")];
        if cmd == "on" || cmd == "off" {
            let mut a = Args::new();
            a.insert("on".into(), json!(cmd == "on"));
            out.push(HostCall::notify(TV, "power_changed", a));
        }
        out
    }

    fn on_event(
        &self,
        _inst: &mut Instance,
        _control: LocalId,
        note: &str,
        args: &Args,
    ) -> Vec<HostCall> {
        // "rx": one line from the dongle's push server (port 9000, declared in the manifest's
        // [[transport]]) — a full state snapshot every time the CEC bus reports a change, not a
        // delta. See CecStateTracker::toJson in the firmware for the exact shape.
        if note != "rx" {
            return Vec::new();
        }
        let Some(line) = args.get("data").and_then(Value::as_str) else {
            return Vec::new();
        };
        let Ok(msg) = serde_json::from_str::<Value>(line) else {
            return Vec::new();
        };

        let mut out = Vec::new();

        if let Some(power) = msg.get("tv_power").and_then(Value::as_str)
            && power != "unknown"
        {
            let mut a = Args::new();
            a.insert("on".into(), json!(power == "on" || power == "turning_on"));
            out.push(HostCall::notify(TV, "power_changed", a));
        }

        // 0 means "unknown" on the dongle's side (see CecStateTracker) — not a real input, so
        // it is not reported as one.
        if let Some(n) = msg.get("active_input").and_then(Value::as_u64).filter(|n| *n > 0) {
            let mut a = Args::new();
            a.insert("connection".into(), json!(1000 + n));
            out.push(HostCall::notify(TV, "input_changed", a));
        }

        // -1 means "unknown" on the dongle's side — CEC volume is only known once something on
        // the bus has reported it, which does not happen until the first change.
        if let Some(v) = msg.get("volume").and_then(Value::as_i64).filter(|v| *v >= 0) {
            let mut a = Args::new();
            a.insert("level".into(), json!(v));
            out.push(HostCall::notify(TV, "volume_changed", a));
        }

        if let Some(mute) = msg.get("mute").and_then(Value::as_bool) {
            let mut a = Args::new();
            a.insert("mute".into(), json!(mute));
            out.push(HostCall::notify(TV, "mute_changed", a));
        }

        out
    }

    fn on_bind(&self, _inst: &mut Instance) -> Vec<HostCall> {
        let mut a = Args::new();
        a.insert("online".into(), json!(true));
        vec![HostCall::notify(TV, "online_changed", a)]
    }

    fn discover(&self, driver_id: &str, state: &Value, input: &Args) -> (SetupStep, Value) {
        self.flow(driver_id, state, input)
    }

    fn setup(&self, driver_id: &str, state: &Value, input: &Args) -> (SetupStep, Value) {
        self.flow(driver_id, state, input)
    }
}

// ---------------------------------------------------------------------------------------
// Setup flow
// ---------------------------------------------------------------------------------------

impl CecDongle {
    fn address_field() -> Field {
        Field {
            name: "address".into(),
            label: "Address".into(),
            kind: "string".into(),
            help: "for example 192.168.1.42 or cec-dongle.local".into(),
            default: None,
            options: Vec::new(),
            required: true,
        }
    }

    fn ask_for_address(state: &Value) -> (SetupStep, Value) {
        // `_cec._tcp` is this project's own service name (see firmware/src/main.cpp), so a
        // responder needs no further probe to confirm what it is — unlike `_http._tcp`, which
        // any ESP8266 web server would also answer.
        let found: Vec<&Value> = state
            .get("mdns_candidates")
            .and_then(Value::as_array)
            .map(|v| v.iter().filter(|f| f.get("service").and_then(Value::as_str) == Some("_cec._tcp")).collect())
            .unwrap_or_default();

        if found.is_empty() {
            return (
                SetupStep::Form {
                    title: "Find your CEC-Dongle".into(),
                    body: "Nothing answered on the network, so enter its address. It shows on \
                           the dongle's own setup wizard, or check your router's device list."
                        .into(),
                    fields: vec![Self::address_field()],
                },
                json!({ "phase": "probe" }),
            );
        }

        let rows: Vec<PickRow> = found
            .iter()
            .filter_map(|f| {
                let address = f.get("address")?.as_str()?.to_string();
                // The mDNS instance name is `<hostname>._cec._tcp.local.` — the hostname is
                // what an installer set on the dongle's own web UI, so it is the only label
                // worth showing.
                let label = f
                    .get("name")
                    .and_then(Value::as_str)
                    .and_then(|n| n.split('.').next())
                    .unwrap_or("CEC-Dongle")
                    .to_string();
                Some(PickRow { value: address.clone(), cells: vec![label, address], note: String::new() })
            })
            .collect();

        (
            SetupStep::Pick {
                title: format!("Found {} CEC-Dongle{}", rows.len(), if rows.len() == 1 { "" } else { "s" }),
                body: "Pick one.".into(),
                columns: vec!["Name".into(), "Address".into()],
                rows,
                field: "address".into(),
                manual: Some(Self::address_field()),
            },
            json!({ "phase": "probe" }),
        )
    }

    fn flow(&self, _driver_id: &str, state: &Value, input: &Args) -> (SetupStep, Value) {
        let phase = state.get("phase").and_then(Value::as_str).unwrap_or("start");

        match phase {
            "start" => Self::ask_for_address(state),

            "probe" => {
                let Some(address) = input.get("address").and_then(Value::as_str).map(str::trim) else {
                    return Self::ask_for_address(state);
                };
                let address = address.to_string();
                (
                    SetupStep::Fetch {
                        request: HttpRequest::new("GET", format!("http://{address}/api/status")),
                        note: "asking the dongle what it is".into(),
                    },
                    json!({ "phase": "probed", "address": address }),
                )
            }

            "probed" => {
                let address = state.get("address").and_then(Value::as_str).unwrap_or_default().to_string();
                let body = input.get("response").and_then(Value::as_str).unwrap_or_default();
                let Ok(status) = serde_json::from_str::<Value>(body) else {
                    return (
                        SetupStep::Failed {
                            reason: format!(
                                "{address} did not answer as a CEC-Dongle. Check the address on \
                                 the dongle's own setup wizard."
                            ),
                        },
                        Value::Null,
                    );
                };
                // `ui_storage` is this firmware's own field, not a generic REST convention —
                // confirming it is present is confirming this is really a CEC-Dongle and not
                // some other device that happens to answer JSON on port 80.
                if status.get("ui_storage").is_none() {
                    return (
                        SetupStep::Failed {
                            reason: format!("{address} answered, but not as a CEC-Dongle."),
                        },
                        Value::Null,
                    );
                }

                let hostname = status
                    .get("hostname")
                    .and_then(Value::as_str)
                    .filter(|s| !s.is_empty())
                    .unwrap_or("CEC-Dongle")
                    .to_string();
                let version = status.get("version").and_then(Value::as_str).unwrap_or("?").to_string();

                (
                    SetupStep::Choose {
                        title: format!("Found {hostname}"),
                        body: String::new(),
                        options: vec![Candidate {
                            label: hostname,
                            kind: "CEC-Dongle".into(),
                            driver_id: "cec_dongle.tv".into(),
                            properties: [("Address".to_string(), json!(address))].into_iter().collect(),
                            verified: format!("answered /api/status, firmware v{version}"),
                            ..Default::default()
                        }],
                        multiple: false,
                    },
                    json!({ "phase": "chosen" }),
                )
            }

            "chosen" => {
                let devices: Vec<Candidate> = input
                    .get("chosen")
                    .and_then(|c| driver_sdk::serde_json::from_value(c.clone()).ok())
                    .unwrap_or_default();
                (SetupStep::done(devices), Value::Null)
            }

            other => (
                SetupStep::Failed { reason: format!("unknown setup phase `{other}`") },
                Value::Null,
            ),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tv() -> Instance {
        let mut inst = Instance::default();
        inst.properties.insert("Address".into(), json!("10.0.0.5"));
        inst
    }

    fn rx(line: &str) -> Args {
        let mut a = Args::new();
        a.insert("data".into(), json!(line));
        a
    }

    #[test]
    fn commands_without_an_address_warn_instead_of_reaching_nowhere() {
        let driver = CecDongle;
        let mut inst = Instance::default();
        let calls = driver.on_command(&mut inst, TV, "on", &Args::new());
        assert!(matches!(calls.as_slice(), [HostCall::Log { level, .. }] if level == "warn"));
    }

    #[test]
    fn on_names_the_tv_on_command() {
        let driver = CecDongle;
        let mut inst = tv();
        let calls = driver.on_command(&mut inst, TV, "on", &Args::new());
        let [HostCall::Http(req), HostCall::Notify { name, args, .. }] = calls.as_slice() else {
            panic!("expected an http call and a notify, got {calls:?}");
        };
        assert!(req.url.ends_with("name=tv_on"), "{}", req.url);
        assert_eq!(name, "power_changed");
        assert_eq!(args.get("on"), Some(&json!(true)));
    }

    #[test]
    fn set_input_names_the_dongles_own_inputn_command() {
        let driver = CecDongle;
        let mut inst = tv();
        let mut a = Args::new();
        a.insert("connection".into(), json!(1003u64));
        let calls = driver.on_command(&mut inst, TV, "set_input", &a);
        let [HostCall::Http(req), HostCall::Notify { .. }] = calls.as_slice() else {
            panic!("expected an http call and a notify, got {calls:?}");
        };
        assert!(req.url.ends_with("name=input3"), "{}", req.url);
    }

    #[test]
    fn set_input_refuses_a_connection_this_driver_never_declared() {
        let driver = CecDongle;
        let mut inst = tv();
        let mut a = Args::new();
        a.insert("connection".into(), json!(9999u64));
        let calls = driver.on_command(&mut inst, TV, "set_input", &a);
        assert!(matches!(calls.as_slice(), [HostCall::Log { level, .. }] if level == "warn"));
    }

    #[test]
    fn set_mute_true_and_false_name_different_commands() {
        let driver = CecDongle;
        let mut inst = tv();

        let mut on = Args::new();
        on.insert("mute".into(), json!(true));
        let calls = driver.on_command(&mut inst, TV, "set_mute", &on);
        let [HostCall::Http(req), ..] = calls.as_slice() else { panic!("{calls:?}") };
        assert!(req.url.ends_with("name=mute_on"), "{}", req.url);

        let mut off = Args::new();
        off.insert("mute".into(), json!(false));
        let calls = driver.on_command(&mut inst, TV, "set_mute", &off);
        let [HostCall::Http(req), ..] = calls.as_slice() else { panic!("{calls:?}") };
        assert!(req.url.ends_with("name=mute_off"), "{}", req.url);
    }

    /// CEC has no absolute volume — declaring has_discrete_volume would be a lie the driver
    /// cannot make good on, so this must warn rather than silently doing nothing.
    #[test]
    fn set_volume_is_refused_with_a_reason() {
        let driver = CecDongle;
        let mut inst = tv();
        let mut a = Args::new();
        a.insert("level".into(), json!(50u64));
        let calls = driver.on_command(&mut inst, TV, "set_volume", &a);
        assert!(matches!(calls.as_slice(), [HostCall::Log { level, .. }] if level == "warn"));
    }

    #[test]
    fn a_pushed_snapshot_becomes_one_notification_per_field_it_actually_carries() {
        let driver = CecDongle;
        let mut inst = tv();
        let line = r#"{"tv_power":"on","active_source":"0x3000","active_input":3,"volume":42,"mute":false,"last_updated_ms":1000}"#;
        let calls = driver.on_event(&mut inst, 0, "rx", &rx(line));
        assert_eq!(calls.len(), 4, "{calls:?}");

        let power_ok = calls.iter().any(|c| matches!(c, HostCall::Notify { name, args, .. }
            if name == "power_changed" && args.get("on") == Some(&json!(true))));
        assert!(power_ok, "{calls:?}");

        let input_ok = calls.iter().any(|c| matches!(c, HostCall::Notify { name, args, .. }
            if name == "input_changed" && args.get("connection") == Some(&json!(1003))));
        assert!(input_ok, "{calls:?}");

        let volume_ok = calls.iter().any(|c| matches!(c, HostCall::Notify { name, args, .. }
            if name == "volume_changed" && args.get("level") == Some(&json!(42))));
        assert!(volume_ok, "{calls:?}");

        let mute_ok = calls.iter().any(|c| matches!(c, HostCall::Notify { name, args, .. }
            if name == "mute_changed" && args.get("mute") == Some(&json!(false))));
        assert!(mute_ok, "{calls:?}");
    }

    /// "unknown" power and a zero active_input are the dongle's own sentinel for "nothing seen
    /// yet" (see CecStateTracker) — reporting them as real values would tell a room the TV is
    /// off before anything on the bus has said so.
    #[test]
    fn unknown_sentinels_are_not_reported_as_real_state() {
        let driver = CecDongle;
        let mut inst = tv();
        let line = r#"{"tv_power":"unknown","active_source":"0x0000","active_input":0,"volume":-1,"mute":false,"last_updated_ms":0}"#;
        let calls = driver.on_event(&mut inst, 0, "rx", &rx(line));
        // mute is a real bool either way, so exactly one notification survives.
        assert_eq!(calls.len(), 1, "{calls:?}");
        assert!(matches!(&calls[0], HostCall::Notify { name, .. } if name == "mute_changed"));
    }

    #[test]
    fn a_non_rx_event_is_ignored() {
        let driver = CecDongle;
        let mut inst = tv();
        let calls = driver.on_event(&mut inst, 0, "http_response", &rx(r#"{"tv_power":"on"}"#));
        assert!(calls.is_empty(), "{calls:?}");
    }

    #[test]
    fn setup_asks_for_an_address_when_nothing_answered_mdns() {
        let driver = CecDongle;
        let (step, next) = driver.discover("cec_dongle.tv", &Value::Null, &Args::new());
        assert!(matches!(step, SetupStep::Form { .. }), "{step:?}");
        assert_eq!(next["phase"], json!("probe"));
    }

    #[test]
    fn setup_offers_a_pick_row_per_mdns_candidate_matching_our_own_service() {
        let driver = CecDongle;
        let state = json!({
            "mdns_candidates": [
                { "name": "cec-dongle._cec._tcp.local.", "service": "_cec._tcp", "address": "10.0.0.9", "port": 80, "txt": {} },
                { "name": "someprinter._ipp._tcp.local.", "service": "_ipp._tcp", "address": "10.0.0.4", "port": 631, "txt": {} },
            ]
        });
        let (step, _) = driver.discover("cec_dongle.tv", &state, &Args::new());
        let SetupStep::Pick { rows, .. } = step else { panic!("expected a Pick step, got {step:?}") };
        assert_eq!(rows.len(), 1, "the printer must not be offered as a CEC-Dongle");
        assert_eq!(rows[0].value, "10.0.0.9");
    }

    #[test]
    fn probed_rejects_a_responder_that_is_not_really_a_cec_dongle() {
        let driver = CecDongle;
        let state = json!({ "phase": "probed", "address": "10.0.0.5" });
        let mut input = Args::new();
        input.insert("response".into(), json!(r#"{"hello":"world"}"#));
        let (step, _) = driver.setup("cec_dongle.tv", &state, &input);
        assert!(matches!(step, SetupStep::Failed { .. }), "{step:?}");
    }

    #[test]
    fn probed_accepts_a_real_status_reply() {
        let driver = CecDongle;
        let state = json!({ "phase": "probed", "address": "10.0.0.5" });
        let mut input = Args::new();
        input.insert(
            "response".into(),
            json!(r#"{"version":"1.0.0","hostname":"cec-dongle","ui_storage":"littlefs-gzip"}"#),
        );
        let (step, _) = driver.setup("cec_dongle.tv", &state, &input);
        let SetupStep::Choose { options, .. } = step else { panic!("expected Choose, got {step:?}") };
        assert_eq!(options.len(), 1);
        assert_eq!(options[0].properties.get("Address"), Some(&json!("10.0.0.5")));
        assert!(options[0].verified.contains("1.0.0"), "{}", options[0].verified);
    }
}

export_driver!(CecDongle);
