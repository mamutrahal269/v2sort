package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"math/rand/v2"
	"os"
	"runtime"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pelletier/go-toml/v2"
	"github.com/schollz/progressbar/v3"
	"github.com/spf13/cobra"
	"github.com/xtls/xray-core/core"
	"github.com/xtls/xray-core/infra/conf"
	_ "github.com/xtls/xray-core/main/distro/all"

	"github.com/mamutrahal269/v2sort/proxy"
	"github.com/mamutrahal269/v2sort/urlconv"
	"github.com/mamutrahal269/v2sort/utils"
)

var version = "dev"

type testResult struct {
	url     string
	metrics []proxy.ConnMetrics
	geo     proxy.Geo
	speed   uint
}

func (r testResult) MarshalJSON() ([]byte, error) {
	var total time.Duration
	for _, m := range r.metrics {
		total += m.TotalTime
	}
	var avgTime int64
	if len(r.metrics) > 0 {
		avgTime = (total / time.Duration(len(r.metrics))).Milliseconds()
	}
	geo := struct {
		IPv4    string `json:"ipv4"`
		IPv6    string `json:"ipv6"`
		Country string `json:"country"`
	}{r.geo.IPv4, r.geo.IPv6, r.geo.Country}

	return json.Marshal(struct {
		URL string `json:"url"`
		Geo struct {
			IPv4    string `json:"ipv4"`
			IPv6    string `json:"ipv6"`
			Country string `json:"country"`
		} `json:"geo"`
		Speed     uint  `json:"speed"`
		AvgTimeMs int64 `json:"avg_time_ms"`
	}{
		URL:       r.url,
		Geo:       geo,
		Speed:     r.speed,
		AvgTimeMs: avgTime,
	})
}

type filterConf struct {
	Allow bool     `toml:"allow"`
	List  []string `toml:"list"`
}

func writeResults(
	outputPath, trashPath string,
	results []testResult,
	trash []utils.Pair[string, error],
	asJSON bool,
) error {
	outFile := os.Stdout
	if outputPath != os.Stdout.Name() {
		f, err := os.OpenFile(outputPath, os.O_TRUNC|os.O_WRONLY|os.O_CREATE, 0o644)
		if err != nil {
			return err
		}
		defer f.Close()
		outFile = f
	}

	trashFile, err := os.OpenFile(trashPath, os.O_TRUNC|os.O_WRONLY|os.O_CREATE, 0o644)
	if err != nil {
		return err
	}
	defer trashFile.Close()

	if asJSON {
		if results == nil {
			results = make([]testResult, 0)
		}
		enc := json.NewEncoder(outFile)
		enc.SetIndent("", "  ")
		if err := enc.Encode(results); err != nil {
			return err
		}
	} else {
		for _, r := range results {
			fmt.Fprintln(outFile, r.url)
		}
	}
	for _, t := range trash {
		fmt.Fprintf(trashFile, "%s : %v\n", t.First, t.Second)
	}

	return nil
}

func validateCfg(
	outbounds []conf.OutboundDetourConfig,
	tagUrl map[string]string,
) ([]conf.OutboundDetourConfig, []utils.Pair[string, error]) {
	var trash []utils.Pair[string, error]
	valid := outbounds[:0]

	oldStdout := os.Stdout
	os.Stdout, _ = os.OpenFile(os.DevNull, os.O_WRONLY, 0)
	for _, o := range outbounds {
		cfg := &conf.Config{
			LogConfig: &conf.LogConfig{
				AccessLog: os.DevNull,
				ErrorLog:  os.DevNull,
				LogLevel:  "none",
			},
			OutboundConfigs: []conf.OutboundDetourConfig{o},
		}
		coreCfg, err := cfg.Build()
		if err != nil {
			trash = append(trash, utils.Pair[string, error]{
				First:  tagUrl[o.Tag],
				Second: err,
			})
			continue
		}
		inst, err := core.New(coreCfg)
		if err != nil {
			trash = append(trash, utils.Pair[string, error]{
				First:  tagUrl[o.Tag],
				Second: err,
			})
			continue
		}
		if err := inst.Start(); err != nil {
			inst.Close()
			trash = append(trash, utils.Pair[string, error]{
				First:  tagUrl[o.Tag],
				Second: err,
			})
			continue
		}

		inst.Close()
		valid = append(valid, o)
	}
	os.Stdout.Close()
	os.Stdout = oldStdout

	return valid, trash
}

func main() {
	// common flags
	var (
		configPath   string
		fetchTimeout time.Duration
		outputPath   string
		trashPath    string
	)
	// TOML config
	var config struct {
		Settings struct {
			Urls         []string   `toml:"urls"`
			SpeedtestUrl string     `toml:"speedtest_url"`
			Protocols    filterConf `toml:"protocols"`
			Countries    filterConf `toml:"countries"`
			Cidrs        filterConf `toml:"cidrs"`
		} `toml:"settings"`
	}
	rootCmd := &cobra.Command{
		Use:   "v2sort",
		Short: "v2sort - proxy sorter and checker",
		Version: fmt.Sprintf(
			"%s (xray-core %s, %s)",
			version, core.Version(), runtime.Version(),
		),
		PersistentPreRunE: func(cmd *cobra.Command, args []string) error {
			data, err := os.ReadFile(configPath)
			if err != nil {
				if cmd.Annotations["configRequired"] != "1" {
					return nil
				}
				return fmt.Errorf("failed to read config file: %v", err)
			}
			if err := toml.Unmarshal(data, &config); err != nil {
				if cmd.Annotations["configRequired"] != "1" {
					return nil
				}
				return fmt.Errorf("failed to parse config: %v", err)
			}
			return nil
		},
	}
	// common to all command modes
	rootCmd.PersistentFlags().StringVarP(
		&configPath,
		"config",
		"c",
		"",
		"path to the config file",
	)
	rootCmd.PersistentFlags().DurationVar(
		&fetchTimeout,
		"fetch-timeout",
		5*time.Second,
		"timeout for fetching subscriptions from internet sources",
	)
	rootCmd.PersistentFlags().StringVarP(
		&outputPath,
		"output",
		"o",
		os.Stdout.Name(),
		"output for processed keys",
	)
	rootCmd.PersistentFlags().StringVarP(
		&trashPath,
		"trash",
		"x",
		os.DevNull,
		"output for dead keys",
	)

	dedupCmd := &cobra.Command{
		Use:   "dedup [sources...]",
		Short: "Deduplicate keys",
		Long: `
Fetches keys from the given sources (files or URLs), parses them and removes
duplicates based on the xray config structure (not string comparison).
Two keys are considered equal if their xray parameters are identical,
even if their URLs differ.`,
		Example: ` 
v2sort dedup proxies.txt
v2sort dedup https://example.com/sub proxies.txt -o deduped.txt
v2sort dedup https://example.com/sub --fetch-timeout 10s -o deduped.txt -x trash.txt`,
		Args: cobra.MinimumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			list, err := fetchAll(args, fetchTimeout)
			if err != nil {
				return err
			}
			list = filterByProtocols(list, config.Settings.Protocols)
			outbounds, tagUrl, trash := urlconv.BuildAll(list, runtime.NumCPU())
			outbounds = dedup(outbounds)
			deduped := make([]testResult, 0, len(outbounds))
			for _, o := range outbounds {
				deduped = append(deduped, testResult{url: tagUrl[o.Tag]})
			}
			return writeResults(outputPath, trashPath, deduped, trash, false)
		},
	}
	validateCmd := &cobra.Command{
		Use:   "validate [sources...]",
		Short: "Validate keys with the xray core",
		Example: `
v2sort validate proxies.txt https://example.com/sub sub.txt
v2sort validate https://example.com/sub --fetch-timeout 10s -o out.txt`,
		Args: cobra.MinimumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			list, err := fetchAll(args, fetchTimeout)
			if err != nil {
				return err
			}
			list = filterByProtocols(list, config.Settings.Protocols)
			outbounds, tagUrl, trash := urlconv.BuildAll(list, runtime.NumCPU())
			outbounds, newTrash := validateCfg(outbounds, tagUrl)
			trash = append(trash, newTrash...)

			success := make([]testResult, len(outbounds))
			for i, o := range outbounds {
				success[i] = testResult{url: tagUrl[o.Tag]}
			}
			return writeResults(outputPath, trashPath, success, trash, false)
		},
	}
	// check flags
	var (
		jobs             int
		random           bool
		minSuccessful    uint
		timeout          time.Duration
		geo              bool
		fragFmt          string
		speedtest        bool
		speedtestTimeout time.Duration
		minSpeed         string
		jsonOutput       bool
	)
	checkCmd := &cobra.Command{
		Use:   "check [sources...]",
		Short: "GET testing",
		Long: `
Fetches keys, deduplicates and validates them, then tests each one through xray-core:
it establishes a connection through the proxy and performs GET requests to the URLs
from the config.

The config (--config) is required. Example minimal config:

  [settings]
  urls = ["https://cp.cloudflare.com"]

A key is considered working if an HTTP 2xx response is received from the required
number of URLs (see --min-successful). Dead keys go to --trash.`,
		Example: `
v2sort check proxies.txt -c v2sort.toml
v2sort check https://example.com/sub -c v2sort.toml -j 100 -t 10s -o ok.txt -x fail.txt
v2sort check proxies.txt -c v2sort.toml -j 50 -n 2 -g --fragment-fmt "%country% %avg_time%"`,

		Args:        cobra.MinimumNArgs(1),
		Annotations: map[string]string{"configRequired": "1"},
		PreRunE: func(cmd *cobra.Command, args []string) error {
			if jobs <= 0 {
				return fmt.Errorf("--jobs must be a positive number")
			}
			if len(config.Settings.Urls) == 0 {
				return fmt.Errorf("settings.urls is empty")
			}
			if speedtest && config.Settings.SpeedtestUrl == "" {
				return fmt.Errorf("settings.speedtest_url is empty")
			}
			if ln := len(config.Settings.Urls); uint(ln) < minSuccessful {
				minSuccessful = uint(ln)
			}
			return nil
		},
		RunE: func(cmd *cobra.Command, args []string) error {
			list, err := fetchAll(args, fetchTimeout)
			if err != nil {
				return err
			}
			list = filterByProtocols(list, config.Settings.Protocols)
			outbounds, tagUrl, trash := urlconv.BuildAll(
				list,
				min(jobs, runtime.NumCPU()),
			)
			outbounds = dedup(outbounds)
			outbounds, newTrash := validateCfg(outbounds, tagUrl)
			trash = append(trash, newTrash...)

			cfg := &conf.Config{
				LogConfig: &conf.LogConfig{
					AccessLog: os.DevNull,
					ErrorLog:  os.DevNull,
					LogLevel:  "none",
				},
				OutboundConfigs: outbounds,
			}
			coreCfg, err := cfg.Build()
			if err != nil {
				return fmt.Errorf("failed to build xray config: %v", err)
			}
			inst, err := core.New(coreCfg)
			if err != nil {
				return fmt.Errorf("failed to create xray instance: %w", err)
			}
			if err := inst.Start(); err != nil {
				return fmt.Errorf("failed to start xray: %w", err)
			}
			defer inst.Close()

			bar := progressbar.NewOptions(
				len(outbounds),
				progressbar.OptionSetDescription("(OK:0 FAIL:0)"),
				progressbar.OptionSetWriter(os.Stderr),
				progressbar.OptionSetWidth(30),
				progressbar.OptionShowCount(),
				progressbar.OptionShowIts(),
				progressbar.OptionSetTheme(progressbar.Theme{
					Saucer:        "█",
					SaucerHead:    "█",
					SaucerPadding: "░",
					BarStart:      "[",
					BarEnd:        "]",
				}),
			)
			var wg sync.WaitGroup
			results := make(chan testResult, len(outbounds))
			sem := make(chan struct{}, jobs)
			var success atomic.Int64
			var failed atomic.Int64

			for _, ob := range outbounds {
				sem <- struct{}{}
				wg.Go(func() {
					defer func() { <-sem }()

					defer func() {
						bar.Add(1)
						bar.Describe(fmt.Sprintf(
							"(OK:%d FAIL:%d)",
							success.Load(),
							failed.Load(),
						))
					}()

					metrics := make([]proxy.ConnMetrics, 0, minSuccessful)
					var g proxy.Geo
					var speed uint

					if random {
						u := config.Settings.Urls[rand.IntN(len(config.Settings.Urls))]
						m, _ := proxy.UrlTest(inst, ob.Tag, u, timeout)
						if m.Code >= 200 && m.Code < 300 {
							metrics = append(metrics, m)
						} else {
							results <- testResult{
								url: tagUrl[ob.Tag],
							}
							failed.Add(1)
							return
						}
					} else {
						var passed uint
						for i, u := range config.Settings.Urls {
							m, _ := proxy.UrlTest(inst, ob.Tag, u, timeout)
							if m.Code >= 200 && m.Code < 300 {
								passed++
							}
							metrics = append(metrics, m)
							if passed == minSuccessful {
								break
							}
							if (passed+(uint(len(config.Settings.Urls))-(uint(i)+1)) < minSuccessful) ||
								(uint(i)+1 == uint(len(config.Settings.Urls))) {
								results <- testResult{
									url: tagUrl[ob.Tag],
								}
								failed.Add(1)
								return
							}
						}
					}
					if geo {
						g, _ = proxy.GetGeo(inst, ob.Tag, timeout)
					}
					if speedtest {
						m, _ := proxy.UrlTest(inst, ob.Tag, config.Settings.SpeedtestUrl, speedtestTimeout)
						speed = uint(m.Speed)
					}
					results <- testResult{
						url:     tagUrl[ob.Tag],
						metrics: metrics,
						geo:     g,
						speed:   speed,
					}
					success.Add(1)
					return
				})
			}
			wg.Wait()
			bar.Finish()
			close(results)

			var working []testResult
			for r := range results {
				if len(r.metrics) == 0 {
					trash = append(trash, utils.Pair[string, error]{
						First:  r.url,
						Second: errors.New("no test passed"),
					})
				} else {
					working = append(working, r)
				}
			}
			if geo {
				working = filterByCountries(working, config.Settings.Countries)
				working = filterByCIDRs(working, config.Settings.Cidrs)
			}
			if speedtest {
				minBytes, err := utils.ParseSpeed(minSpeed)
				if err != nil {
					return fmt.Errorf("--min-speed: %w", err)
				}
				working = filterBySpeed(working, minBytes)
			}
			if fragFmt != "" {
				for i := range working {
					working[i].url = fmtFragment(working[i], fragFmt)
				}
			}
			fmt.Printf("\n")
			return writeResults(outputPath, trashPath, working, trash, jsonOutput)
		},
	}
	checkCmd.MarkPersistentFlagRequired("config")
	checkCmd.Flags().IntVarP(
		&jobs,
		"jobs",
		"j",
		runtime.NumCPU(),
		"number of parallel workers",
	)
	checkCmd.Flags().BoolVarP(
		&random,
		"random",
		"r",
		false,
		"pick one random URL for the test",
	)
	checkCmd.Flags().UintVarP(
		&minSuccessful,
		"min-successful",
		"n",
		1,
		"minimum number of successful GET tests",
	)
	checkCmd.MarkFlagsMutuallyExclusive("min-successful", "random")
	checkCmd.Flags().DurationVarP(
		&timeout,
		"timeout",
		"t",
		5*time.Second,
		"GET test timeout",
	)
	checkCmd.Flags().BoolVarP(
		&geo,
		"geo",
		"g",
		false,
		"collect geo data for the key",
	)
	checkCmd.Flags().StringVar(
		&fragFmt,
		"fragment-fmt",
		"",
		"key name template (%ipv4%, %ipv6%, %country%, %speed%, %speed_kib%, %speed_mib%, %avg_time%)",
	)
	checkCmd.Flags().BoolVarP(
		&speedtest,
		"speedtest",
		"S",
		false,
		"run a speedtest for the key",
	)
	checkCmd.Flags().DurationVar(
		&speedtestTimeout,
		"speedtest-timeout",
		5*time.Second,
		"speedtest timeout",
	)
	checkCmd.Flags().StringVar(
		&minSpeed,
		"min-speed",
		"0",
		"minimum speed (Mbps, MB, KiB, GiB...)",
	)
	checkCmd.Flags().BoolVar(
		&jsonOutput,
		"json",
		false,
		"output the report as JSON",
	)

	rootCmd.AddCommand(dedupCmd)
	rootCmd.AddCommand(validateCmd)
	rootCmd.AddCommand(checkCmd)

	if err := rootCmd.Execute(); err != nil {
		os.Exit(1)
	}
}
