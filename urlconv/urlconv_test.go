package urlconv

import (
	"encoding/base64"
	"encoding/json"
	stdurl "net/url"
	"reflect"
	"testing"

	"github.com/nlnwa/whatwg-url/url"
	"github.com/xtls/xray-core/infra/conf"
)

// ---------- helpers ----------

func parseURL(t *testing.T, raw string) *url.Url {
	t.Helper()
	u, err := url.Parse(raw)
	if err != nil {
		t.Fatalf("url.Parse(%q): %v", raw, err)
	}
	return u
}

func decodeSettings[T any](t *testing.T, ob *conf.OutboundDetourConfig) T {
	t.Helper()
	if ob == nil {
		t.Fatal("outbound is nil")
	}
	if ob.Settings == nil {
		t.Fatal("outbound settings is nil")
	}
	var v T
	if err := json.Unmarshal(*ob.Settings, &v); err != nil {
		t.Fatalf("json.Unmarshal(%s): %v", string(*ob.Settings), err)
	}
	return v
}

func b64(s string) string {
	return base64.StdEncoding.EncodeToString([]byte(s))
}

func mustConvert(t *testing.T, conv func(string, string) (*conf.OutboundDetourConfig, error), raw string) *conf.OutboundDetourConfig {
	t.Helper()
	ob, err := conv(raw, "0")
	if err != nil {
		t.Fatalf("%q: unexpected error: %v", raw, err)
	}
	if ob == nil {
		t.Fatalf("%q: nil outbound", raw)
	}
	return ob
}

func networkName(sc *conf.StreamConfig) string {
	if sc == nil || sc.Network == nil {
		return ""
	}
	return string(*sc.Network)
}

// ---------- converters ----------

func TestVless(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name    string
		raw     string
		want    vlessSettings
		stream  func(*testing.T, *conf.StreamConfig)
		wantErr bool
	}{
		{
			name: "base defaults",
			raw:  "vless://11111111-2222-3333-4444-555555555555@example.com:443",
			want: vlessSettings{
				Address:    "example.com",
				Port:       443,
				ID:         "11111111-2222-3333-4444-555555555555",
				Encryption: "none",
			},
			stream: func(t *testing.T, sc *conf.StreamConfig) {
				t.Helper()
				if sc.Security != "none" {
					t.Errorf("security = %q, want none", sc.Security)
				}
				if got := networkName(sc); got != "raw" {
					t.Errorf("network = %q, want raw", got)
				}
				if sc.RAWSettings == nil {
					t.Error("rawSettings is nil")
				}
			},
		},
		{
			name: "flow and percent encoded id",
			raw:  "vless://user%40example.com@example.com:8443?flow=xtls-rprx-vision",
			want: vlessSettings{
				Address:    "example.com",
				Port:       8443,
				ID:         "user@example.com",
				Encryption: "none",
				Flow:       "xtls-rprx-vision",
			},
		},
		{
			name: "tls params",
			raw:  "vless://id@example.com:443?security=tls&sni=sni.example&alpn=h2,http/1.1&fp=firefox",
			want: vlessSettings{Address: "example.com", Port: 443, ID: "id", Encryption: "none"},
			stream: func(t *testing.T, sc *conf.StreamConfig) {
				t.Helper()
				if sc.Security != "tls" {
					t.Fatalf("security = %q, want tls", sc.Security)
				}
				if sc.TLSSettings == nil {
					t.Fatal("tlsSettings is nil")
				}
				if sc.TLSSettings.ServerName != "sni.example" {
					t.Errorf("serverName = %q", sc.TLSSettings.ServerName)
				}
				if sc.TLSSettings.Fingerprint != "firefox" {
					t.Errorf("fingerprint = %q", sc.TLSSettings.Fingerprint)
				}
				if sc.TLSSettings.ALPN == nil || !reflect.DeepEqual([]string(*sc.TLSSettings.ALPN), []string{"h2", "http/1.1"}) {
					t.Errorf("alpn = %v", sc.TLSSettings.ALPN)
				}
			},
		},
		{
			name: "websocket",
			raw:  "vless://id@example.com:443?security=tls&type=ws&path=%2Fws&host=example.com&alpn=h2",
			want: vlessSettings{Address: "example.com", Port: 443, ID: "id", Encryption: "none"},
			stream: func(t *testing.T, sc *conf.StreamConfig) {
				t.Helper()
				if got := networkName(sc); got != "websocket" {
					t.Fatalf("network = %q, want websocket", got)
				}
				if sc.WSSettings == nil {
					t.Fatal("wsSettings is nil")
				}
				if sc.WSSettings.Path != "/ws" {
					t.Errorf("path = %q", sc.WSSettings.Path)
				}
				if sc.WSSettings.Host != "example.com" {
					t.Errorf("host = %q", sc.WSSettings.Host)
				}
				// websocket transport must not force ALPN
				if sc.TLSSettings != nil && sc.TLSSettings.ALPN != nil {
					t.Errorf("alpn = %v, want nil", *sc.TLSSettings.ALPN)
				}
			},
		},
		{name: "wrong scheme", raw: "vmess://id@example.com:443", wantErr: true},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			if tt.wantErr {
				if _, err := Vless(tt.raw, "0"); err == nil {
					t.Fatal("expected error, got nil")
				}
				return
			}

			ob := mustConvert(t, Vless, tt.raw)
			if ob.Protocol != "vless" {
				t.Errorf("protocol = %q, want vless", ob.Protocol)
			}
			if got := decodeSettings[vlessSettings](t, ob); got != tt.want {
				t.Errorf("settings = %+v, want %+v", got, tt.want)
			}
			if tt.stream != nil {
				if ob.StreamSetting == nil {
					t.Fatal("streamSettings is nil")
				}
				tt.stream(t, ob.StreamSetting)
			}
		})
	}
}

type vlessSettings struct {
	Address    string `json:"address"`
	Port       uint16 `json:"port"`
	ID         string `json:"id"`
	Encryption string `json:"encryption"`
	Flow       string `json:"flow"`
}

type vmessSettings struct {
	Address     string `json:"address"`
	Port        uint16 `json:"port"`
	ID          string `json:"id"`
	Security    string `json:"security"`
	Experiments string `json:"experiments"`
}

func vmessURL(payload string) string {
	return "vmess://" + b64(payload)
}

func TestVmess(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name    string
		raw     string
		want    vmessSettings
		stream  func(*testing.T, *conf.StreamConfig)
		wantErr bool
	}{
		{
			name: "base64 json websocket tls",
			raw: vmessURL(`{"v":"2","add":"example.com","port":"443","id":"uuid",` +
				`"scy":"aes-128-gcm","net":"ws","type":"none","host":"example.com",` +
				`"path":"/ws","tls":"tls","sni":"sni.example","fp":"chrome"}`),
			want: vmessSettings{
				Address:  "example.com",
				Port:     443,
				ID:       "uuid",
				Security: "aes-128-gcm",
			},
			stream: func(t *testing.T, sc *conf.StreamConfig) {
				t.Helper()
				if sc.Security != "tls" {
					t.Fatalf("security = %q, want tls", sc.Security)
				}
				if got := networkName(sc); got != "websocket" {
					t.Fatalf("network = %q, want websocket", got)
				}
				if sc.WSSettings == nil || sc.WSSettings.Path != "/ws" || sc.WSSettings.Host != "example.com" {
					t.Errorf("wsSettings = %+v", sc.WSSettings)
				}
				if sc.TLSSettings == nil || sc.TLSSettings.ServerName != "sni.example" {
					t.Errorf("tlsSettings = %+v", sc.TLSSettings)
				}
			},
		},
		{
			name: "defaults and tls alpn on raw transport",
			raw: vmessURL(`{"add":"example.com","port":443,"id":"uuid",` +
				`"net":"raw","tls":"tls","alpn":"h2,http/1.1","fp":"firefox"}`),
			want: vmessSettings{
				Address:  "example.com",
				Port:     443,
				ID:       "uuid",
				Security: "auto",
			},
			stream: func(t *testing.T, sc *conf.StreamConfig) {
				t.Helper()
				if got := networkName(sc); got != "raw" {
					t.Fatalf("network = %q, want raw", got)
				}
				if sc.TLSSettings == nil {
					t.Fatal("tlsSettings is nil")
				}
				if sc.TLSSettings.ALPN == nil || !reflect.DeepEqual([]string(*sc.TLSSettings.ALPN), []string{"h2", "http/1.1"}) {
					t.Errorf("alpn = %v", sc.TLSSettings.ALPN)
				}
			},
		},
		{
			name: "kcp seed",
			raw: vmessURL(`{"add":"example.com","port":"443","id":"uuid","net":"kcp",` +
				`"type":"wechat","path":"seed-value"}`),
			want: vmessSettings{Address: "example.com", Port: 443, ID: "uuid", Security: "auto"},
			stream: func(t *testing.T, sc *conf.StreamConfig) {
				t.Helper()
				if got := networkName(sc); got != "mkcp" {
					t.Fatalf("network = %q, want mkcp", got)
				}
				if sc.FinalMask == nil || len(sc.FinalMask.Udp) == 0 {
					t.Fatal("finalmask udp is empty")
				}
				if sc.FinalMask.Udp[0].Type != "mkcp-legacy" {
					t.Errorf("mask type = %q, want mkcp-legacy", sc.FinalMask.Udp[0].Type)
				}
			},
		},
		{
			name: "grpc",
			raw: vmessURL(`{"add":"example.com","port":"443","id":"uuid","net":"grpc",` +
				`"type":"multi","path":"service-name"}`),
			want: vmessSettings{Address: "example.com", Port: 443, ID: "uuid", Security: "auto"},
			stream: func(t *testing.T, sc *conf.StreamConfig) {
				t.Helper()
				if got := networkName(sc); got != "grpc" {
					t.Fatalf("network = %q, want grpc", got)
				}
				if sc.GRPCSettings == nil {
					t.Fatal("grpcSettings is nil")
				}
				if sc.GRPCSettings.ServiceName != "service-name" {
					t.Errorf("serviceName = %q", sc.GRPCSettings.ServiceName)
				}
				if !sc.GRPCSettings.MultiMode {
					t.Error("multiMode = false, want true")
				}
			},
		},
		{
			name:    "invalid json",
			raw:     vmessURL("not a json"),
			wantErr: true,
		},
		{
			name:    "wrong scheme",
			raw:     "vless://id@example.com:443",
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			if tt.wantErr {
				if _, err := Vmess(tt.raw, "0"); err == nil {
					t.Fatal("expected error, got nil")
				}
				return
			}

			ob := mustConvert(t, Vmess, tt.raw)
			if ob.Protocol != "vmess" {
				t.Errorf("protocol = %q, want vmess", ob.Protocol)
			}
			if got := decodeSettings[vmessSettings](t, ob); got != tt.want {
				t.Errorf("settings = %+v, want %+v", got, tt.want)
			}
			if tt.stream != nil {
				if ob.StreamSetting == nil {
					t.Fatal("streamSettings is nil")
				}
				tt.stream(t, ob.StreamSetting)
			}
		})
	}
}

type ssSettings struct {
	Address  string `json:"address"`
	Port     uint16 `json:"port"`
	Method   string `json:"method"`
	Password string `json:"password"`
}

func TestShadowsocks(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name    string
		raw     string
		want    ssSettings
		wantErr bool
	}{
		{
			name: "legacy base64",
			raw:  "ss://" + b64("aes-256-gcm:secret@example.com:8388"),
			want: ssSettings{Address: "example.com", Port: 8388, Method: "aes-256-gcm", Password: "secret"},
		},
		{
			name: "sip002 base64 userinfo",
			raw:  "ss://" + b64("chacha20-ietf-poly1305:secret") + "@example.com:8388#node",
			want: ssSettings{Address: "example.com", Port: 8388, Method: "chacha20-ietf-poly1305", Password: "secret"},
		},
		{
			name: "plain userinfo",
			raw:  "ss://aes-128-gcm:secret@example.com:8388",
			want: ssSettings{Address: "example.com", Port: 8388, Method: "aes-128-gcm", Password: "secret"},
		},
		{
			name:    "invalid legacy base64",
			raw:     "ss://" + b64("no-colon-here"),
			wantErr: true,
		},
		{
			name:    "invalid sip002 base64",
			raw:     "ss://" + b64("no-colon-here") + "@example.com:8388",
			wantErr: true,
		},
		{
			name:    "wrong scheme",
			raw:     "vless://id@example.com:443",
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			if tt.wantErr {
				if _, err := Shadowsocks(tt.raw, "0"); err == nil {
					t.Fatal("expected error, got nil")
				}
				return
			}

			ob := mustConvert(t, Shadowsocks, tt.raw)
			if ob.Protocol != "shadowsocks" {
				t.Errorf("protocol = %q, want shadowsocks", ob.Protocol)
			}
			if got := decodeSettings[ssSettings](t, ob); got != tt.want {
				t.Errorf("settings = %+v, want %+v", got, tt.want)
			}
		})
	}
}

type trojanSettings struct {
	Address  string `json:"address"`
	Port     uint16 `json:"port"`
	Password string `json:"password"`
}

func TestTrojan(t *testing.T) {
	t.Parallel()

	ob := mustConvert(t, Trojan, "trojan://sec%20ret@example.com:443?security=reality&sni=sni.example")
	if ob.Protocol != "trojan" {
		t.Errorf("protocol = %q, want trojan", ob.Protocol)
	}
	want := trojanSettings{Address: "example.com", Port: 443, Password: "sec ret"}
	if got := decodeSettings[trojanSettings](t, ob); got != want {
		t.Errorf("settings = %+v, want %+v", got, want)
	}
	if ob.StreamSetting == nil {
		t.Fatal("streamSettings is nil")
	}
	// trojan always forces TLS, even if the link asks for reality
	if ob.StreamSetting.Security != "tls" {
		t.Errorf("security = %q, want tls", ob.StreamSetting.Security)
	}

	if _, err := Trojan("trojan://example.com:443", "0"); err == nil {
		t.Error("missing userinfo: expected error, got nil")
	}
	if _, err := Trojan("vless://id@example.com:443", "0"); err == nil {
		t.Error("wrong scheme: expected error, got nil")
	}
}

type httpSettings struct {
	Address string `json:"address"`
	Port    uint16 `json:"port"`
	User    string `json:"user"`
	Pass    string `json:"pass"`
}

func TestHTTP(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name     string
		raw      string
		want     httpSettings
		wantSec  string
		wantErr  bool
		wantProt string
	}{
		{
			name:     "http",
			raw:      "http://user:pass@example.com:8080",
			want:     httpSettings{Address: "example.com", Port: 8080, User: "user", Pass: "pass"},
			wantSec:  "none",
			wantProt: "http",
		},
		{
			name:     "https",
			raw:      "https://user%40example.com:pa%20ss@example.com:8443",
			want:     httpSettings{Address: "example.com", Port: 8443, User: "user@example.com", Pass: "pa ss"},
			wantSec:  "tls",
			wantProt: "http",
		},
		{
			name:    "wrong scheme",
			raw:     "socks5://user:pass@example.com:1080",
			wantErr: true,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			if tt.wantErr {
				if _, err := Http(tt.raw, "0"); err == nil {
					t.Fatal("expected error, got nil")
				}
				return
			}

			ob := mustConvert(t, Http, tt.raw)
			if ob.Protocol != tt.wantProt {
				t.Errorf("protocol = %q, want %q", ob.Protocol, tt.wantProt)
			}
			if got := decodeSettings[httpSettings](t, ob); got != tt.want {
				t.Errorf("settings = %+v, want %+v", got, tt.want)
			}
			if ob.StreamSetting == nil {
				t.Fatal("streamSettings is nil")
			}
			if ob.StreamSetting.Security != tt.wantSec {
				t.Errorf("security = %q, want %q", ob.StreamSetting.Security, tt.wantSec)
			}
		})
	}
}

func TestSocks5(t *testing.T) {
	t.Parallel()

	ob := mustConvert(t, Socks5, "socks5://user:pass@example.com:1080")
	if ob.Protocol != "socks" {
		t.Errorf("protocol = %q, want socks", ob.Protocol)
	}
	want := httpSettings{Address: "example.com", Port: 1080, User: "user", Pass: "pass"}
	if got := decodeSettings[httpSettings](t, ob); got != want {
		t.Errorf("settings = %+v, want %+v", got, want)
	}

	if _, err := Socks5("http://user:pass@example.com:8080", "0"); err == nil {
		t.Error("wrong scheme: expected error, got nil")
	}
}

type hysteriaSettings struct {
	Version int    `json:"version"`
	Address string `json:"address"`
	Port    uint16 `json:"port"`
}

func TestHysteria2(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name     string
		raw      string
		wantAuth string
	}{
		{
			name:     "user and password",
			raw:      "hysteria2://user:pass@example.com:443",
			wantAuth: "user:pass",
		},
		{
			name:     "user only",
			raw:      "hy2://user@example.com:443",
			wantAuth: "user",
		},
		{
			name:     "percent encoded credentials",
			raw:      "hysteria2://user%40example.com:pa%20ss@example.com:443",
			wantAuth: "user@example.com:pa ss",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			ob := mustConvert(t, Hysteria2, tt.raw)
			if ob.Protocol != "hysteria" {
				t.Errorf("protocol = %q, want hysteria", ob.Protocol)
			}
			want := hysteriaSettings{Version: 2, Address: "example.com", Port: 443}
			if got := decodeSettings[hysteriaSettings](t, ob); got != want {
				t.Errorf("settings = %+v, want %+v", got, want)
			}

			if ob.StreamSetting == nil {
				t.Fatal("streamSettings is nil")
			}
			// regression: hysteria2 is always TLS
			if ob.StreamSetting.Security != "tls" {
				t.Errorf("security = %q, want tls", ob.StreamSetting.Security)
			}
			if got := networkName(ob.StreamSetting); got != "hysteria" {
				t.Errorf("network = %q, want hysteria", got)
			}
			if ob.StreamSetting.HysteriaSettings == nil {
				t.Fatal("hysteriaSettings is nil")
			}
			if ob.StreamSetting.HysteriaSettings.Version != 2 {
				t.Errorf("hysteria version = %d, want 2", ob.StreamSetting.HysteriaSettings.Version)
			}
			if ob.StreamSetting.HysteriaSettings.Auth != tt.wantAuth {
				t.Errorf("auth = %q, want %q", ob.StreamSetting.HysteriaSettings.Auth, tt.wantAuth)
			}
		})
	}

	if _, err := Hysteria2("vless://id@example.com:443", "0"); err == nil {
		t.Error("wrong scheme: expected error, got nil")
	}
}

// TestConvertersBuildable makes sure the generated outbounds can be converted
// into an xray config. It is fully offline: no core instance is started.
func TestConvertersBuildable(t *testing.T) {
	t.Parallel()

	vmess := vmessURL(`{"add":"127.0.0.1","port":"443","id":"uuid","net":"ws","path":"/ws","tls":"tls"}`)

	cases := []struct {
		name string
		raw  string
		conv func(string, string) (*conf.OutboundDetourConfig, error)
	}{
		{"vless", "vless://uuid@127.0.0.1:443?security=tls&type=ws&path=%2Fws", Vless},
		{"vmess", vmess, Vmess},
		{"shadowsocks", "ss://" + b64("aes-256-gcm:secret") + "@127.0.0.1:8388", Shadowsocks},
		{"trojan", "trojan://secret@127.0.0.1:443", Trojan},
		{"http", "http://user:pass@127.0.0.1:8080", Http},
		{"https", "https://user:pass@127.0.0.1:8443", Http},
		{"socks5", "socks5://user:pass@127.0.0.1:1080", Socks5},
		{"hysteria2", "hysteria2://user:pass@127.0.0.1:443", Hysteria2},
	}

	for _, tt := range cases {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			ob := mustConvert(t, tt.conv, tt.raw)
			cfg := &conf.Config{
				OutboundConfigs: []conf.OutboundDetourConfig{*ob},
			}
			if _, err := cfg.Build(); err != nil {
				t.Fatalf("conf.Config.Build(): %v", err)
			}
		})
	}
}

// ---------- buildStreamSetting ----------

func TestBuildStreamSettingTLS(t *testing.T) {
	t.Parallel()

	t.Run("tls params", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t,
			"vless://id@example.com:443?security=tls&sni=sni.example&alpn=h2,http/1.1"+
				"&fp=firefox&pcs=a~b;c&ech=ech-value"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if sc.Security != "tls" {
			t.Fatalf("security = %q, want tls", sc.Security)
		}
		if sc.Address == nil || sc.Address.String() != "example.com" {
			t.Errorf("address = %v", sc.Address)
		}
		if sc.Port != 443 {
			t.Errorf("port = %d, want 443", sc.Port)
		}
		if sc.TLSSettings == nil {
			t.Fatal("tlsSettings is nil")
		}
		if sc.TLSSettings.ServerName != "sni.example" {
			t.Errorf("serverName = %q", sc.TLSSettings.ServerName)
		}
		if sc.TLSSettings.Fingerprint != "firefox" {
			t.Errorf("fingerprint = %q", sc.TLSSettings.Fingerprint)
		}
		if sc.TLSSettings.PinnedPeerCertSha256 != "a,b,c" {
			t.Errorf("pinnedPeerCertSha256 = %q, want a,b,c", sc.TLSSettings.PinnedPeerCertSha256)
		}
		if sc.TLSSettings.ECHConfigList != "ech-value" {
			t.Errorf("echConfigList = %q", sc.TLSSettings.ECHConfigList)
		}
		if sc.TLSSettings.ALPN == nil || !reflect.DeepEqual([]string(*sc.TLSSettings.ALPN), []string{"h2", "http/1.1"}) {
			t.Errorf("alpn = %v", sc.TLSSettings.ALPN)
		}
	})

	t.Run("xtls maps to tls", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t, "vless://id@example.com:443?security=xtls"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if sc.Security != "tls" {
			t.Errorf("security = %q, want tls", sc.Security)
		}
	})

	t.Run("fingerprint default", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t, "vless://id@example.com:443?security=tls"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if sc.TLSSettings == nil || sc.TLSSettings.Fingerprint != "chrome" {
			t.Errorf("fingerprint = %+v, want chrome", sc.TLSSettings)
		}
	})

	t.Run("unknown security is none", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t, "vless://id@example.com:443?security=unknown"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if sc.Security != "none" {
			t.Errorf("security = %q, want none", sc.Security)
		}
		if sc.TLSSettings != nil {
			t.Errorf("tlsSettings = %+v, want nil", sc.TLSSettings)
		}
	})
}

func TestBuildStreamSettingReality(t *testing.T) {
	t.Parallel()

	sc, err := buildStreamSetting(parseURL(t,
		"vless://id@example.com:443?security=reality&sni=sni.example&sid=abcd"+
			"&pqv=verify&fp=firefox&spx=%2Fspider&pbk=public-key"))
	if err != nil {
		t.Fatalf("buildStreamSetting: %v", err)
	}
	if sc.Security != "reality" {
		t.Fatalf("security = %q, want reality", sc.Security)
	}
	if sc.REALITYSettings == nil {
		t.Fatal("realitySettings is nil")
	}
	r := sc.REALITYSettings
	if r.ServerName != "sni.example" {
		t.Errorf("serverName = %q", r.ServerName)
	}
	if r.ShortId != "abcd" {
		t.Errorf("shortId = %q", r.ShortId)
	}
	if r.Mldsa65Verify != "verify" {
		t.Errorf("mldsa65Verify = %q", r.Mldsa65Verify)
	}
	if r.Fingerprint != "firefox" {
		t.Errorf("fingerprint = %q", r.Fingerprint)
	}
	if r.SpiderX != "/spider" {
		t.Errorf("spiderX = %q", r.SpiderX)
	}
	if r.Password != "public-key" {
		t.Errorf("password = %q", r.Password)
	}

	t.Run("defaults", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t, "vless://id@example.com:443?security=reality"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if sc.REALITYSettings == nil {
			t.Fatal("realitySettings is nil")
		}
		if sc.REALITYSettings.Fingerprint != "chrome" {
			t.Errorf("fingerprint = %q, want chrome", sc.REALITYSettings.Fingerprint)
		}
		if sc.REALITYSettings.SpiderX != "/" {
			t.Errorf("spiderX = %q, want /", sc.REALITYSettings.SpiderX)
		}
	})
}

func TestBuildStreamSettingTransports(t *testing.T) {
	t.Parallel()

	t.Run("default raw", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t, "vless://id@example.com:443"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if got := networkName(sc); got != "raw" {
			t.Errorf("network = %q, want raw", got)
		}
		if sc.RAWSettings == nil {
			t.Error("rawSettings is nil")
		}
	})

	t.Run("xhttp", func(t *testing.T) {
		t.Parallel()

		extra := stdurl.QueryEscape(`{"x":1}`)
		sc, err := buildStreamSetting(parseURL(t,
			"vless://id@example.com:443?type=xhttp&host=example.com&path=%2Fx&mode=packet-up&extra="+extra))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if got := networkName(sc); got != "xhttp" {
			t.Fatalf("network = %q, want xhttp", got)
		}
		if sc.XHTTPSettings == nil {
			t.Fatal("xhttpSettings is nil")
		}
		if sc.XHTTPSettings.Host != "example.com" || sc.XHTTPSettings.Path != "/x" || sc.XHTTPSettings.Mode != "packet-up" {
			t.Errorf("xhttpSettings = %+v", sc.XHTTPSettings)
		}
		if got := string(sc.XHTTPSettings.Extra); got != `{"x":1}` {
			t.Errorf("extra = %q", got)
		}
		if sc.SplitHTTPSettings == nil {
			t.Error("splitHTTPSettings is nil")
		}
	})

	t.Run("splithttp alias", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t, "vless://id@example.com:443?type=splithttp&path=%2Fs"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if got := networkName(sc); got != "xhttp" {
			t.Errorf("network = %q, want xhttp", got)
		}
	})

	t.Run("mkcp defaults", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t, "vless://id@example.com:443?type=mkcp"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if got := networkName(sc); got != "mkcp" {
			t.Errorf("network = %q, want mkcp", got)
		}
		if sc.KCPSettings == nil {
			t.Fatal("kcpSettings is nil")
		}
		if sc.KCPSettings.Mtu == nil || *sc.KCPSettings.Mtu != 1350 {
			t.Errorf("mtu = %v, want 1350", sc.KCPSettings.Mtu)
		}
		if sc.KCPSettings.Tti == nil || *sc.KCPSettings.Tti != 50 {
			t.Errorf("tti = %v, want 50", sc.KCPSettings.Tti)
		}
		if sc.FinalMask == nil || len(sc.FinalMask.Udp) != 1 || sc.FinalMask.Udp[0].Type != "mkcp-legacy" {
			t.Errorf("finalmask = %+v", sc.FinalMask)
		}
	})

	t.Run("mkcp overrides", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t,
			"vless://id@example.com:443?type=kcp&mtu=1400&tti=20&header=wechat&seed=seed-value"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if sc.KCPSettings.Mtu == nil || *sc.KCPSettings.Mtu != 1400 {
			t.Errorf("mtu = %v, want 1400", sc.KCPSettings.Mtu)
		}
		if sc.KCPSettings.Tti == nil || *sc.KCPSettings.Tti != 20 {
			t.Errorf("tti = %v, want 20", sc.KCPSettings.Tti)
		}
		if sc.FinalMask == nil || len(sc.FinalMask.Udp) != 1 {
			t.Fatalf("finalmask = %+v", sc.FinalMask)
		}
		var settings map[string]string
		if err := json.Unmarshal(*sc.FinalMask.Udp[0].Settings, &settings); err != nil {
			t.Fatalf("decode mask settings: %v", err)
		}
		if settings["header"] != "wechat" || settings["value"] != "seed-value" {
			t.Errorf("mask settings = %v", settings)
		}
	})

	t.Run("grpc", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t,
			"vless://id@example.com:443?type=grpc&serviceName=svc&authority=auth"+
				"&user_agent=ua&mode=multi"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if got := networkName(sc); got != "grpc" {
			t.Errorf("network = %q, want grpc", got)
		}
		if sc.GRPCSettings == nil {
			t.Fatal("grpcSettings is nil")
		}
		if sc.GRPCSettings.ServiceName != "svc" || sc.GRPCSettings.Authority != "auth" {
			t.Errorf("grpcSettings = %+v", sc.GRPCSettings)
		}
		if sc.GRPCSettings.UserAgent != "ua" {
			t.Errorf("userAgent = %q", sc.GRPCSettings.UserAgent)
		}
		if !sc.GRPCSettings.MultiMode {
			t.Error("multiMode = false, want true")
		}
	})

	t.Run("websocket", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t,
			"vless://id@example.com:443?security=tls&alpn=h2&type=ws&host=example.com&path=%2Fws&ed=2048"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if got := networkName(sc); got != "websocket" {
			t.Errorf("network = %q, want websocket", got)
		}
		if sc.WSSettings == nil {
			t.Fatal("wsSettings is nil")
		}
		if sc.WSSettings.Path != "/ws?ed=2048" {
			t.Errorf("path = %q, want /ws?ed=2048", sc.WSSettings.Path)
		}
		if sc.WSSettings.Host != "example.com" {
			t.Errorf("host = %q", sc.WSSettings.Host)
		}
		if sc.TLSSettings != nil && sc.TLSSettings.ALPN != nil {
			t.Errorf("alpn = %v, want nil", sc.TLSSettings.ALPN)
		}
	})

	t.Run("websocket default path", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t, "vless://id@example.com:443?type=ws"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if sc.WSSettings == nil || sc.WSSettings.Path != "/" {
			t.Errorf("wsSettings = %+v", sc.WSSettings)
		}
	})

	t.Run("httpupgrade", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t, "vless://id@example.com:443?type=httpupgrade&host=example.com&path=%2Fhu"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if got := networkName(sc); got != "httpupgrade" {
			t.Errorf("network = %q, want httpupgrade", got)
		}
		if sc.HTTPUPGRADESettings == nil {
			t.Fatal("httpupgradeSettings is nil")
		}
		if sc.HTTPUPGRADESettings.Path != "/hu" || sc.HTTPUPGRADESettings.Host != "example.com" {
			t.Errorf("httpupgradeSettings = %+v", sc.HTTPUPGRADESettings)
		}
	})
}

func TestBuildStreamSettingHysteria(t *testing.T) {
	t.Parallel()

	t.Run("obfs and mport", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t,
			"hysteria2://user:pass@example.com:443?security=tls&obfs=salamander&obfs-password=secret&mport=1000-2000"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if sc.Security != "tls" {
			t.Errorf("security = %q, want tls", sc.Security)
		}
		if got := networkName(sc); got != "hysteria" {
			t.Fatalf("network = %q, want hysteria", got)
		}
		if sc.HysteriaSettings == nil {
			t.Fatal("hysteriaSettings is nil")
		}
		if sc.HysteriaSettings.Version != 2 {
			t.Errorf("version = %d, want 2", sc.HysteriaSettings.Version)
		}
		if sc.HysteriaSettings.Auth != "user:pass" {
			t.Errorf("auth = %q", sc.HysteriaSettings.Auth)
		}
		if sc.FinalMask == nil || len(sc.FinalMask.Udp) != 1 {
			t.Fatalf("finalmask = %+v", sc.FinalMask)
		}
		if sc.FinalMask.Udp[0].Type != "salamander" {
			t.Errorf("mask type = %q", sc.FinalMask.Udp[0].Type)
		}
		var settings map[string]string
		if err := json.Unmarshal(*sc.FinalMask.Udp[0].Settings, &settings); err != nil {
			t.Fatalf("decode mask settings: %v", err)
		}
		if settings["password"] != "secret" {
			t.Errorf("mask settings = %v", settings)
		}
		if sc.FinalMask.QuicParams == nil || len(sc.FinalMask.QuicParams.UdpHop.PortList.Range) != 1 {
			t.Fatalf("quicParams = %+v", sc.FinalMask.QuicParams)
		}
		r := sc.FinalMask.QuicParams.UdpHop.PortList.Range[0]
		if r.From != 1000 || r.To != 2000 {
			t.Errorf("mport range = %d-%d, want 1000-2000", r.From, r.To)
		}
	})

	t.Run("user only", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t, "hy2://user@example.com:443"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if sc.HysteriaSettings == nil || sc.HysteriaSettings.Auth != "user" {
			t.Errorf("hysteriaSettings = %+v", sc.HysteriaSettings)
		}
		if sc.FinalMask == nil {
			t.Error("finalmask is nil")
		}
	})

	t.Run("percent encoded credentials", func(t *testing.T) {
		t.Parallel()

		sc, err := buildStreamSetting(parseURL(t, "hysteria2://user%40example.com:pa%20ss@example.com:443"))
		if err != nil {
			t.Fatalf("buildStreamSetting: %v", err)
		}
		if sc.HysteriaSettings == nil || sc.HysteriaSettings.Auth != "user@example.com:pa ss" {
			t.Errorf("auth = %+v", sc.HysteriaSettings)
		}
	})
}

func TestBuildStreamSettingFinalMaskQuery(t *testing.T) {
	t.Parallel()

	fm := stdurl.QueryEscape(`{"tcp":[{"type":"http","settings":{"host":["example.com"]}}]}`)
	sc, err := buildStreamSetting(parseURL(t, "vless://id@example.com:443?fm="+fm))
	if err != nil {
		t.Fatalf("buildStreamSetting: %v", err)
	}
	if sc.FinalMask == nil || len(sc.FinalMask.Tcp) != 1 {
		t.Fatalf("finalmask = %+v", sc.FinalMask)
	}
	if sc.FinalMask.Tcp[0].Type != "http" {
		t.Errorf("mask type = %q, want http", sc.FinalMask.Tcp[0].Type)
	}
}

func TestBuildStreamSettingAddress(t *testing.T) {
	t.Parallel()

	tests := []struct {
		name string
		raw  string
		want string
		port uint16
	}{
		{name: "domain", raw: "vless://id@example.com:443", want: "example.com", port: 443},
		{name: "ipv4", raw: "vless://id@10.1.2.3:8080", want: "10.1.2.3", port: 8080},
		{name: "ipv6", raw: "vless://id@[2001:db8::1]:8443", want: "[2001:db8::1]", port: 8443},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()

			sc, err := buildStreamSetting(parseURL(t, tt.raw))
			if err != nil {
				t.Fatalf("buildStreamSetting: %v", err)
			}
			if sc.Address == nil || sc.Address.String() != tt.want {
				t.Errorf("address = %v, want %q", sc.Address, tt.want)
			}
			if sc.Port != tt.port {
				t.Errorf("port = %d, want %d", sc.Port, tt.port)
			}
		})
	}
}

// ---------- small helpers ----------

func TestGetQuery(t *testing.T) {
	t.Parallel()

	params := parseURL(t, "vless://id@example.com:443?a=&b=value&c=%20trimmed%20").SearchParams()

	tests := []struct {
		name string
		keys []string
		def  string
		want string
	}{
		{name: "single", keys: []string{"b"}, def: "def", want: "value"},
		{name: "skips empty and uses next", keys: []string{"a", "b"}, def: "def", want: "value"},
		{name: "trims value", keys: []string{"c"}, def: "def", want: "trimmed"},
		{name: "default", keys: []string{"missing"}, def: "def", want: "def"},
		{name: "default with several keys", keys: []string{"x", "y"}, def: "def", want: "def"},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			t.Parallel()
			if got := getQuery(params, tt.def, tt.keys...); got != tt.want {
				t.Errorf("getQuery() = %q, want %q", got, tt.want)
			}
		})
	}
}

func TestPercentDecode(t *testing.T) {
	t.Parallel()

	tests := []struct {
		in   string
		want string
	}{
		{in: "simple", want: "simple"},
		{in: "a%20b", want: "a b"},
		{in: "user%40example.com", want: "user@example.com"},
		{in: "%zz", want: "%zz"},
	}

	for _, tt := range tests {
		if got := percentDecode(tt.in); got != tt.want {
			t.Errorf("percentDecode(%q) = %q, want %q", tt.in, got, tt.want)
		}
	}
}

func TestDerefOr(t *testing.T) {
	t.Parallel()

	value := "value"
	if got := derefOr(&value, "default"); got != "value" {
		t.Errorf("derefOr(&value) = %q, want value", got)
	}
	if got := derefOr[string](nil, "default"); got != "default" {
		t.Errorf("derefOr(nil) = %q, want default", got)
	}
}

func TestIsExpectedSchemes(t *testing.T) {
	t.Parallel()

	u := parseURL(t, "hysteria2://user@example.com:443")
	if err := isExpectedSchemes(u, "hysteria2", "hy2"); err != nil {
		t.Errorf("unexpected error: %v", err)
	}
	if err := isExpectedSchemes(u, "vless"); err == nil {
		t.Error("expected error, got nil")
	}
}
