package main

import (
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

	"v2sort/proxy"
	"v2sort/urlconv"
	"v2sort/utils"
)

const version = "0.12.1"

type testResult struct {
	url     string
	metrics []proxy.ConnMetrics
	geo     proxy.Geo
	speed   uint
}
type filterConf struct {
	Allow bool     `toml:"allow"`
	List  []string `toml:"list"`
}

func writeResults(
	outputPath, trashPath string,
	urls []string,
	trash []utils.Pair[string, error],
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

	for _, u := range urls {
		fmt.Fprintln(outFile, u)
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
				AccessLog: "/dev/null",
				ErrorLog:  "/dev/null",
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
	// общие флаги
	var (
		configPath   string
		fetchTimeout time.Duration
		outputPath   string
		trashPath    string
	)
	// TOML конфиг
	var config struct {
		XrayLog struct {
			AccessLog   string `toml:"access"`
			ErrorLog    string `toml:"error"`
			LogLevel    string `toml:"loglevel"`
			DNSLog      bool   `toml:"dns_log"`
			MaskAddress string `toml:"mask_address"`
		} `toml:"xray_log"`
		Settings struct {
			Urls         []string   `toml:"urls"`
			SpeedtestUrl string     `toml:"speedtest_url"`
			UserAgent    string     `toml:"user_agent"`
			Hwid         string     `toml:"hwid"`
			Protocols    filterConf `toml:"protocols"`
			Countries    filterConf `toml:"countries"`
			Cidrs        filterConf `toml:"cidrs"`
		} `toml:"settings"`
	}
	rootCmd := &cobra.Command{
		Use:   "v2sort",
		Short: "v2sort - сортировщик и чекер прокси",
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
				return fmt.Errorf("не удалось прочитать файл конфига: %v", err)
			}
			if err := toml.Unmarshal(data, &config); err != nil {
				if cmd.Annotations["configRequired"] != "1" {
					return nil
				}
				return fmt.Errorf("ошибка парсинга конфига: %v", err)
			}
			return nil
		},
	}
	// общие для всех режимов команды
	rootCmd.PersistentFlags().StringVarP(
		&configPath,
		"config",
		"c",
		"",
		"путь к файлу конфигурации",
	)
	rootCmd.PersistentFlags().DurationVar(
		&fetchTimeout,
		"fetch-timeout",
		5*time.Second,
		"таймаут загрузки подписок из интернет источников",
	)
	rootCmd.PersistentFlags().StringVarP(
		&outputPath,
		"output",
		"o",
		os.Stdout.Name(),
		"вывод для обработанных ключей",
	)
	rootCmd.PersistentFlags().StringVarP(
		&trashPath,
		"trash",
		"x",
		os.DevNull,
		"вывод для мусорных ключей",
	)

	dedupCmd := &cobra.Command{
		Use:   "dedup [источники...]",
		Short: "Дедупликация ключей",
		Long: `
Получает ключи из указанных источников (файлы или URL),
парсит их и удаляет дубликаты на основе структуры конфига (не строкового сравнения).
Два ключа считаются одинаковыми если идентичны их xray-параметры, даже если URL различаются.`,
		Example: ` 
v2sort dedup proxies.txt
v2sort dedup https://example.com/sub proxies.txt -o deduped.txt
v2sort dedup https://example.com/sub --fetch-timeout 10s -o deduped.txt -x trash.txt`,
		Args: cobra.MinimumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			list, err := fetchAll(args, config.Settings.UserAgent, config.Settings.Hwid, fetchTimeout)
			if err != nil {
				return err
			}
			list = filterByProtocols(list, config.Settings.Protocols)
			outbounds, tagUrl, trash := urlconv.BuildAll(list, runtime.NumCPU())
			outbounds = dedup(outbounds)
			deduped := make([]string, 0, len(outbounds))
			for _, o := range outbounds {
				deduped = append(deduped, tagUrl[o.Tag])
			}
			return writeResults(outputPath, trashPath, deduped, trash)
		},
	}
	validateCmd := &cobra.Command{
		Use:   "validate [источники...]",
		Short: "Проверка ключей ядром xray",
		Example: `
v2sort validate proxies.txt https://example.com/sub sub.txt
v2sort validate https://example.com/sub --fetch-timeout 10s -o out.txt`,
		Args: cobra.MinimumNArgs(1),
		RunE: func(cmd *cobra.Command, args []string) error {
			list, err := fetchAll(args, config.Settings.UserAgent, config.Settings.Hwid, fetchTimeout)
			if err != nil {
				return err
			}
			list = filterByProtocols(list, config.Settings.Protocols)
			outbounds, tagUrl, trash := urlconv.BuildAll(list, runtime.NumCPU())
			outbounds, newTrash := validateCfg(outbounds, tagUrl)
			trash = append(trash, newTrash...)

			success := make([]string, len(outbounds))
			for i, o := range outbounds {
				success[i] = tagUrl[o.Tag]
			}
			return writeResults(outputPath, trashPath, success, trash)
		},
	}
	// флаги для check
	var (
		jobs             int
		random           bool
		minSuccessful    uint
		timeout          time.Duration
		geo              bool
		fragFmt          string
		speedtest        bool
		speedtestTimeout time.Duration
	)
	checkCmd := &cobra.Command{
		Use:   "check [источники...]",
		Short: "GET-тестирование",
		Long: `
Получает ключи, дедуплицирует, валидирует, затем тестирует каждый через xray-core:
устанавливает соединение через прокси и выполняет GET-запросы к URLs из конфига.

Конфиг (--config) обязателен. Пример минимального конфига:

  [settings]
  urls = ["https://cp.cloudflare.com"]

Ключ считается рабочим если получен HTTP 2xx от нужного числа URLs (см. --min-successful).
Нерабочие ключи попадают в --trash.`,
		Example: `
v2sort check proxies.txt -c v2sort.toml
v2sort check https://example.com/sub -c v2sort.toml -j 100 -t 10s -o ok.txt -x fail.txt
v2sort check proxies.txt -c v2sort.toml -j 50 -n 2 -g --fragment-fmt "%country% %avg_time%"`,

		Args:        cobra.MinimumNArgs(1),
		Annotations: map[string]string{"configRequired": "1"},
		PreRunE: func(cmd *cobra.Command, args []string) error {
			if jobs <= 0 {
				return fmt.Errorf("-jobs должно быть положительным числом")
			}
			if len(config.Settings.Urls) == 0 {
				return fmt.Errorf("settings.urls пустой")
			}
			if speedtest && config.Settings.SpeedtestUrl == "" {
				return fmt.Errorf("settings.speedtest_url пустой")
			}
			if ln := len(config.Settings.Urls); uint(ln) < minSuccessful {
				minSuccessful = uint(ln)
			}
			return nil
		},
		RunE: func(cmd *cobra.Command, args []string) error {
			list, err := fetchAll(args, config.Settings.UserAgent, config.Settings.Hwid, fetchTimeout)
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
					AccessLog:   config.XrayLog.AccessLog,
					ErrorLog:    config.XrayLog.ErrorLog,
					LogLevel:    config.XrayLog.LogLevel,
					DNSLog:      config.XrayLog.DNSLog,
					MaskAddress: config.XrayLog.MaskAddress,
				},
				OutboundConfigs: outbounds,
			}
			coreCfg, err := cfg.Build()
			if err != nil {
				return fmt.Errorf("не удалось собрать конфиг xray: %v", err)
			}
			inst, err := core.New(coreCfg)
			if err != nil {
				return fmt.Errorf("не удалось создать инстанс xray: %w", err)
			}
			if err := inst.Start(); err != nil {
				return fmt.Errorf("не удалось запустить xray: %w", err)
			}
			defer inst.Close()

			bar := progressbar.NewOptions(
				len(outbounds),
				progressbar.OptionSetDescription("(OK:0 FAIL:0)"),
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
				wg.Add(1)
				sem <- struct{}{}
				go func(ob conf.OutboundDetourConfig) {
					defer wg.Done()
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
				}(ob)
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
			if fragFmt != "" {
				for i := range working {
					working[i].url = fmtFragment(working[i], fragFmt)
				}
			}
			urls := make([]string, 0, len(working))
			for _, w := range working {
				urls = append(urls, w.url)
			}

			return writeResults(outputPath, trashPath, urls, trash)
		},
	}
	checkCmd.MarkPersistentFlagRequired("config")
	checkCmd.Flags().IntVarP(
		&jobs,
		"jobs",
		"j",
		runtime.NumCPU(),
		"количество параллельных воркеров",
	)
	checkCmd.Flags().BoolVarP(
		&random,
		"random",
		"r",
		false,
		"выбирать один рандомный URL для теста",
	)
	checkCmd.Flags().UintVarP(
		&minSuccessful,
		"min-successful",
		"n",
		1,
		"минимальное количество успешных GET-тестов",
	)
	checkCmd.MarkFlagsMutuallyExclusive("min-successful", "random")
	checkCmd.Flags().DurationVarP(
		&timeout,
		"timeout",
		"t",
		5*time.Second,
		"таймаут GET-теста",
	)
	checkCmd.Flags().BoolVarP(
		&geo,
		"geo",
		"g",
		false,
		"получать геоданные ключа",
	)
	checkCmd.Flags().StringVar(
		&fragFmt,
		"fragment-fmt",
		"",
		"шаблон имени ключа(%ipv4%, %ipv6%, %country%, %speed%, %speed_kib%, %speed_mib%, %avg_time%)",
	)
	checkCmd.Flags().BoolVarP(
		&speedtest,
		"speedtest",
		"S",
		false,
		"делать speedtest для ключа",
	)
	checkCmd.Flags().DurationVar(
		&speedtestTimeout,
		"speedtest-timeout",
		5*time.Second,
		"таймаут speedtest",
	)

	rootCmd.AddCommand(dedupCmd)
	rootCmd.AddCommand(validateCmd)
	rootCmd.AddCommand(checkCmd)

	if err := rootCmd.Execute(); err != nil {
		os.Exit(1)
	}
}
