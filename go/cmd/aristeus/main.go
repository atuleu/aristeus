package main

import (
	"log/slog"
	"os"

	"github.com/jessevdk/go-flags"
	"github.com/lmittmann/tint"
	"golang.org/x/term"
)

type Options struct {
	Verbose []bool `short:"V" long:"verbose" description:"more verbose output"`
}

var opts = &Options{}
var parser = flags.NewParser(opts, flags.Default)

func main() {
	if err := execute(); err != nil {
		slog.Error("Unhandled error", slog.String("error", err.Error()))
		os.Exit(1)
	}
}

func (opts *Options) setupLogger() {
	isTerminal := term.IsTerminal(int(os.Stderr.Fd()))
	level := slog.LevelInfo
	if len(opts.Verbose) >= 1 {
		level = slog.LevelDebug
	}

	options := &tint.Options{
		NoColor: !isTerminal,
		Level:   level,
		ReplaceAttr: func(groups []string, a slog.Attr) slog.Attr {
			switch a.Value.Kind() {
			case slog.KindAny:
				if _, ok := a.Value.Any().(error); ok {
					return tint.Attr(9, a)
				}
				return a

			case slog.KindString:
				switch a.Key {
				case "error":
					return tint.Attr(9, a)
				case "module":
					return tint.Attr(4, a)
				default:
					return a
				}

			default:
				return a
			}
		},
	}

	handler := tint.NewTextHandler(os.Stderr, options)
	logger := slog.New(handler)
	slog.SetDefault(logger)
}

func execute() error {
	parser.CommandHandler = func(command flags.Commander, args []string) error {
		opts.setupLogger()
		return command.Execute(args)
	}

	_, err := parser.Parse()
	if err != nil {
		if flags.WroteHelp(err) == true {
			os.Exit(0)
		}
		os.Exit(2)
	}
	return nil
}
