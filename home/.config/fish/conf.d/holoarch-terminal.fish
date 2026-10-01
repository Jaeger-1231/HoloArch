# Print Fastfetch once per interactive Kitty window. Nested shells inherit the
# window marker, while a new Kitty process/window gets its own marker.
if status is-interactive; and isatty stdout; and set -q KITTY_WINDOW_ID; and type -q fastfetch
    set -l holoarch_terminal_id "$KITTY_PID:$KITTY_WINDOW_ID"
    if not set -q HOLOARCH_FASTFETCH_SESSION; or test "$HOLOARCH_FASTFETCH_SESSION" != "$holoarch_terminal_id"
        set -gx HOLOARCH_FASTFETCH_SESSION "$holoarch_terminal_id"
        command fastfetch
    end
end
