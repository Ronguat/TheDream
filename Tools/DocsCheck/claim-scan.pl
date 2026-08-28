#!/usr/bin/env perl
# claim-scan.pl -- shortlists capability claims that name no surface and no date.
#
#   perl Tools/DocsCheck/claim-scan.pl <file>...
#
# A capability claim asserts that something cannot be done. It is dangerous only when it
# omits WHICH surface was tried and WHEN, because an MCP-only result then reads identically
# to one tested across MCP, editor Python and C++. This shortlists blocks that carry an
# absence phrase and name a callable, but no surface word and no ISO date.
#
# Blocks are bullets or blank-line-separated paragraphs. Headers and table rows are skipped;
# so are blocks whose language is *about* claims rather than making one.
#
# --working-only truncates each file at its first "## 2026-" header, which is how the decision
# log's append-only archive is excluded: those entries are immutable and each already sits under
# a dated header, so only the working sections above them are live enough to fix.
#
# Exit 0 = every capability claim is qualified, 1 = shortlist printed.
use strict; use warnings;

# The scripting surfaces, plus the three "no surface reaches this" categories. Declaring a claim
# "engine behaviour", "fixture behaviour" or "machine fact" is a complete answer to which-surface: it tells the reader
# not to go hunting a wider one -- so it satisfies the check while the date still dates it.
my $SURFACE  = qr/\b(toolset|MCP|Python|C\+\+|reflection|Slate|Bash|PowerShell|registry
                    |ENGINE_API|UNREALED_API|ANIMGRAPH_API|KISMET_API|UE_API
                    |engine\ behaviour|fixture\ behaviour|machine\ fact|gameplay\ rule)(?!\w)/x;
my $ABSENCE  = qr/\b(cannot|can't|unable\ to|impossible|no\ way\ to|there\ is\ no\ |nothing\ can\ 
                    |not\ possible|unsupported|not\ supported|not\ available|not\ exposed
                    |not\ reachable|unreadable|not\ scriptable|no\ route|not\ proven|refuses)\b/xi;
my $META     = qr/(before\ concluding|nearly\ always\ wrong|the\ tell\ is|enumerate\ before
                   |a\ verdict\ about|how\ to\ treat|re-test\ any\ limit|an\ empty\ result
                   |proves\ only\ that|is\ not\ \*"it)/xi;
# A callable: Owner::method, Owner.method, snake_case_fn, or a *Tools/*Toolset name.
my $CALLABLE = qr/`(?:[A-Za-z_][A-Za-z0-9_]*\.)?[A-Za-z_][A-Za-z0-9_]*(?:::|\.)[A-Za-z_][A-Za-z0-9_]*\(?\)?`
                  |`[a-z][a-z0-9]*(?:_[a-z0-9]+){1,}\(?\)?`
                  |`[A-Z][A-Za-z0-9]*Tools?(?:et)?`/x;

my $working_only = 0;
@ARGV = grep { $_ eq '--working-only' ? (($working_only = 1), 0) : 1 } @ARGV;

my $bad = 0;
for my $file (@ARGV) {
    open(my $fh, '<:raw', $file) or die "claim-scan: cannot open $file\n";
    local $/; my $text = <$fh>; close $fh; $text =~ s/\r//g;
    if ($working_only && $text =~ /^## 2026-\d\d-\d\d/m) {
        $text = substr($text, 0, $-[0]);
    }

    my (@blocks, @lines); my $cur = ''; my $n = 1; my $start = 1;
    for my $line (split /\n/, $text) {
        if ($line =~ /^\s*$/ || $line =~ /^\s*- / || $line =~ /^#/) {
            if ($cur =~ /\S/) { push @blocks, $cur; push @lines, $start }
            $cur = $line; $start = $n;
        } else { $cur .= "\n" . $line }
        $n++;
    }
    if ($cur =~ /\S/) { push @blocks, $cur; push @lines, $start }

    for my $i (0 .. $#blocks) {
        my $b = $blocks[$i];
        next if $b =~ /^#/ || $b =~ /^\s*\|/;
        next unless $b =~ $ABSENCE;
        next if     $b =~ $META;
        next unless $b =~ $CALLABLE;
        my $has_surface = ($b =~ $SURFACE);
        my $has_date    = ($b =~ /2026-\d\d-\d\d/);
        next if $has_surface && $has_date;
        my $why = (!$has_surface && !$has_date) ? 'no surface, no date'
                : (!$has_surface ? 'no surface' : 'no date');
        (my $flat = $b) =~ s/\n/ /g; $flat =~ s/^\s+//;
        printf "  %s:%d [%s] %.90s\n", $file, $lines[$i], $why, $flat;
        $bad = 1;
    }
}
exit $bad;
