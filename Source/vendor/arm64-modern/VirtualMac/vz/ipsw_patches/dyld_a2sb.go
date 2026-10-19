package dyld

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/blacktop/ipsw/internal/commands/dsc"
	"github.com/blacktop/ipsw/internal/utils"
	"github.com/blacktop/ipsw/pkg/dyld"
	"github.com/blacktop/ipsw/pkg/symbols"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

func init() {
	DyldCmd.AddCommand(A2SBatchCmd)
	A2SBatchCmd.Flags().String("cache", "", "Path to .a2s addr to sym cache file")
	viper.BindPFlag("dyld.a2sb.cache", A2SBatchCmd.Flags().Lookup("cache"))
}

// A2SBatchCmd batch-resolves many unslid addresses (DSC opened once).
// Output: one "0xADDR\tSYMBOL\tIMAGE" line per input address.
var A2SBatchCmd = &cobra.Command{
	Use:           "a2sb <DSC> <ADDRFILE>",
	Short:         "Batch lookup symbols at unslid addresses (one per line in ADDRFILE)",
	Args:          cobra.ExactArgs(2),
	SilenceErrors: true,
	RunE: func(cmd *cobra.Command, args []string) error {
		f, err := dyld.Open(filepath.Clean(args[0]))
		if err != nil {
			return err
		}
		defer f.Close()
		cacheFile := viper.GetString("dyld.a2sb.cache")
		if len(cacheFile) > 0 {
			if err := f.OpenOrCreateA2SCache(cacheFile); err != nil {
				return err
			}
		}
		data, err := os.ReadFile(args[1])
		if err != nil {
			return err
		}
		for _, line := range strings.Fields(string(data)) {
			a, e := utils.ConvertStrToInt(line)
			if e != nil {
				continue
			}
			var s *dsc.SymbolLookup
			if len(cacheFile) > 0 {
				s, e = dsc.LookupSymbol(f, a)
			} else {
				s, e = dsc.DirectLookupSymbol(f, a)
			}
			if e != nil || s == nil {
				fmt.Printf("%#x\t\t\n", a)
				continue
			}
			fmt.Printf("%#x\t%s\t%s\n", a, symbols.FormatSymbol(s.Symbol, false), s.Image)
		}
		return nil
	},
}
