fish_add_path ~/.local/bin
if status is-interactive
    set -g fish_greeting ""
    if type -q starship
        starship init fish | source
    end
    if type -q zoxide
        zoxide init fish --cmd cd | source
    end
end

function y
	set tmp (mktemp -t "yazi-cwd.XXXXXX")
	yazi $argv --cwd-file="$tmp"
	if read -z cwd < "$tmp"; and [ -n "$cwd" ]; and [ "$cwd" != "$PWD" ]
		builtin cd -- "$cwd"
	end
	rm -f -- "$tmp"
end

function cat
	command bat --theme="base16" -- $argv
end

function ls
	command eza --icons=auto -- $argv
end

function lt
	command eza --icons=auto --tree -- $argv
end

function la
	command eza -l --icons=auto -- $argv
end

# grub
abbr grub 'LANGUAGE=en_US.UTF-8 LANG=en_US.UTF-8 sudo grub-mkconfig -o /boot/grub/grub.cfg'
# fa运行fastfetch
abbr fa fastfetch
abbr reboot 'systemctl reboot'
function sl
	command sl | lolcat
end
function 滚
	~/.local/bin/holoarch update
end
function raw
	command ~/.config/scripts/random-anime-wallpaper.sh $argv
end

# Added by LM Studio CLI (lms)
fish_add_path ~/.lmstudio/bin
# End of LM Studio CLI section
