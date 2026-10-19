#!/usr/bin/perl
use strict;
use warnings;
# Native on-disk Catalina Mach-O files only. Never edits a dyld cache.
my ($kind, $input, $output) = @ARGV;
die "usage: patch-guest-image.pl coredisplay|compiler|service INPUT OUTPUT\n" unless @ARGV == 3;
open my $in, '<:raw', $input or die "$input: $!\n";
local $/; my $bytes = <$in>; close $in;
die "Expected native thin x86_64 Mach-O: $input\n"
    unless length($bytes) >= 32 && unpack('V', substr($bytes,0,4)) == 0xfeedfacf
    && unpack('V', substr($bytes,4,4)) == 0x01000007;
my @text;
my @symtab;
my ($commands, $command_bytes) = unpack('VV', substr($bytes,16,8));
my $cursor = 32;
die "Invalid Mach-O command size\n" if $cursor + $command_bytes > length($bytes);
for (1..$commands) {
    die "Truncated command\n" if $cursor + 8 > 32 + $command_bytes;
    my ($command,$size) = unpack('VV',substr($bytes,$cursor,8));
    die "Invalid command\n" if $size < 8 || $cursor + $size > 32 + $command_bytes;
    @symtab = unpack('VVVV',substr($bytes,$cursor+8,16)) if $command == 2 && $size >= 24;
    if ($command == 0x19 && $size >= 72) {
        my $nsects = unpack('V',substr($bytes,$cursor+64,4));
        die "Invalid section count\n" if 72 + $nsects*80 > $size;
        for my $index (0..$nsects-1) {
            my $section = $cursor+72+$index*80;
            my $name = substr($bytes,$section,16); $name =~ s/\0.*//s;
            my $segment = substr($bytes,$section+16,16); $segment =~ s/\0.*//s;
            if ($name eq '__text' && $segment eq '__TEXT') {
                my $length = unpack('Q<',substr($bytes,$section+40,8));
                my $offset = unpack('V',substr($bytes,$section+48,4));
                die "Invalid text bounds\n" if $offset+$length > length($bytes);
                my $address = unpack('Q<',substr($bytes,$section+32,8));
                push @text, [$offset,$length,$address];
            }
        }
    }
    $cursor += $size;
}
die "Missing unique executable text section\n" unless @text == 1;
my %symbols;
if (@symtab) {
    my ($offset,$count,$strings,$string_length)=@symtab;
    die "Invalid symbol table\n" if $offset+$count*16 > length($bytes) || $strings+$string_length > length($bytes);
    for my $index (0..$count-1) {
        my ($nameoff,$type,$section,$description,$address)=unpack('VCCvQ<',substr($bytes,$offset+$index*16,16));
        next if ($type & 0xe) != 0xe || ($type & 0xe0) || $nameoff >= $string_length;
        next if $address < $text[0][2] || $address >= $text[0][2]+$text[0][1];
        my $end=index($bytes,"\0",$strings+$nameoff);
        die "Unterminated symbol\n" if $end<0 || $end >= $strings+$string_length;
        $symbols{substr($bytes,$strings+$nameoff,$end-$strings-$nameoff)}=$address;
    }
}
sub patch_unique {
    my ($old,$new,$code) = @_;
    die "Patch changes file length\n" unless length($old)==length($new);
    my ($start,$length) = (0,length($bytes));
    if ($code) {
        die "Missing native function symbol $code\n" unless exists $symbols{$code};
        my $address=$symbols{$code};
        my @later=sort {$a<=>$b} grep {$_>$address} values %symbols;
        my $end=@later ? $later[0] : $text[0][2]+$text[0][1];
        ($start,$length)=($text[0][0]+$address-$text[0][2],$end-$address);
    }
    my @matches;
    for my $pattern ($old,$new) {
        my $position = $start;
        while (($position=index($bytes,$pattern,$position)) >= 0 && $position+length($pattern) <= $start+$length) {
            push @matches, $position; $position++;
        }
    }
    die "Unknown or ambiguous $kind patch ($input): ".scalar(@matches)." matches\n" unless @matches==1;
    substr($bytes,$matches[0],length($new),$new);
    printf "%s: matched patch at file offset 0x%x\n", $kind, $matches[0];
}
if ($kind eq 'coredisplay') {
    patch_unique(pack('H*','0f44d089d083c804380d'),pack('H*','0f44d089d083c814380d'),'__ZN11CoreDisplay16UseIOPresentmentEv');
    patch_unique(pack('H*','4883f8ffb801000000bb080000000f44d8'),pack('H*','4883f8ffb810000000bb080000000f44d8'),'_GetGPUVendorForFramebufferService');
} elsif ($kind eq 'compiler') {
    my $old='AppleIntelHD4000GraphicsMTLDriver.bundle/Contents/MacOS/libMTLIntelCompilerPlugin.dylib';
    my $new='AppleParavirtGPUMetal.bundle/Contents/Resources/libAppleParavirtCompilerPlugin.dylib';
    patch_unique($old,$new.("\0" x (length($old)-length($new))),0);
    die "Unsupported native compiler family\n" unless $bytes =~ m{/GPUCompiler\.framework/Versions/(?:3802|3902)/Libraries/libLLVM\.dylib};
} elsif ($kind eq 'service') {
    if (index($bytes, '/MTLCompiler.framework/Versions/%d/MTLCompiler') >= 0) {
        # High Sierra constructs its versioned path. Keep the legacy A branch;
        # route the PVG experiment's versioned compiler to a private directory.
        patch_unique('/System/Library/PrivateFrameworks/MTLCompiler.framework/Versions/%d/MTLCompiler',
                     '/System/Library/PrivateFrameworks/MTLCompiler.framework/Versions/P3/MTLCompiler',0);
    } else {
    my $family = index($bytes, '/MTLCompiler.framework/Versions/3802/MTLCompiler') >= 0 ? '380' : '390';
    patch_unique('/System/Library/PrivateFrameworks/MTLCompiler.framework/Versions/'.$family.'2/MTLCompiler',
                 '/System/Library/PrivateFrameworks/MTLCompiler.framework/Versions/'.$family.'3/MTLCompiler',0);
    }
} else { die "Unknown patch kind\n"; }
open my $out, '>:raw', $output or die "$output: $!\n";
print $out $bytes; close $out or die "$output: $!\n";
chmod 0755, $output or die "chmod: $!\n";
