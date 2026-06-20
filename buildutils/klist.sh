find arch boot buildutils drivers include kernel lib \
  -type f \
  \( -name "*.c" -o -name "*.h" -o -name "*.s" \
     -o -name "*.sh" -o -name "*.ld" -o -name "*.conf" \) \
  ! -path "include/limine.h" \
  | sort \
  | xargs wc -l \
  | awk '
    BEGIN {
      BOLD  = "\033[1m"
      CYAN  = "\033[36m"
      GREEN = "\033[32m"
      DIM   = "\033[2m"
      RESET = "\033[0m"
      SEP   = "─────────────────────────────────────────────────────────────"
      printf "\n%s%s  Kunix Millennium — Line Count%s\n", BOLD, CYAN, RESET
      printf "%s  %s%s\n\n", DIM, SEP, RESET
      lastdir = ""
    }
    !/total$/ && NF==2 {
      split($2, parts, "/")
      dir = parts[1]
      if (dir != lastdir) {
        if (lastdir != "") printf "\n"
        printf "  %s%s%s/\n", BOLD, CYAN, dir, RESET
        lastdir = dir
      }
      printf "    %-48s %s%5d%s\n", $2, GREEN, $1, RESET
    }
    /total$/ {
      printf "\n%s  %s%s\n", DIM, SEP, RESET
      printf "  %-50s %s%s%5d%s\n\n", "TOTAL", BOLD, GREEN, $1, RESET
    }
  '