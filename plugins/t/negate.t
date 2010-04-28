#! /usr/bin/perl -w -I ..
#
# negate checks
# Need check_dummy to work for testing
#
# $Id: negate.pl 1717 2007-05-24 08:53:50Z tonvoon $
#

use strict;
use Test::More;
use NPTest;

# 15 tests in the first part and 32 in the last loop
plan tests => 47;

my $res;

my $PWD = $ENV{PWD};

$res = NPTest->testCmd( "./negate" );
is( $res->return_code, 3, "Not enough parameters");
like( $res->output, "/Could not parse arguments/", "Could not parse arguments");

$res = NPTest->testCmd( "./negate bobthebuilder" );
is( $res->return_code, 3, "Require full path" );
like( $res->output, "/Require path to command/", "Appropriate error message");

$res = NPTest->testCmd( "./negate $PWD/check_dummy 0 'a dummy okay'" );
is( $res->return_code, 2, "OK changed to CRITICAL" );
is( $res->output, "OK: a dummy okay", "Output as expected" );

$res = NPTest->testCmd( "./negate '$PWD/check_dummy 0 redsweaterblog'");
is( $res->return_code, 2, "OK => CRIT with a single quote for command to run" );
is( $res->output, "OK: redsweaterblog", "Output as expected" );

$res = NPTest->testCmd( "./negate $PWD/check_dummy 1 'a warn a day keeps the managers at bay'" );
is( $res->return_code, 1, "WARN stays same" );

$res = NPTest->testCmd( "./negate $PWD/check_dummy 3 mysterious");
is( $res->return_code, 3, "UNKNOWN stays same" );

$res = NPTest->testCmd( "./negate \"$PWD/check_dummy 0 'a dummy okay'\"" );
is( $res->output, "OK: a dummy okay", "Checking slashed quotes - the single quotes are re-evaluated at shell" );

# Output is "OK: a" because check_dummy only returns the first arg
$res = NPTest->testCmd( "./negate $PWD/check_dummy 0 a dummy okay" );
is( $res->output, "OK: a", "Multiple args passed as arrays" );

$res = NPTest->testCmd( "./negate $PWD/check_dummy 0 'a dummy okay'" );
is( $res->output, "OK: a dummy okay", "The quoted string is passed through to subcommand correctly" );

$res = NPTest->testCmd( "./negate '$PWD/check_dummy 0' 'a dummy okay'" );
is( $res->output, "No data returned from command", "Bad command, as expected (trying to execute './check_dummy 0')");

$res = NPTest->testCmd( './negate $PWD/check_dummy 0 \'$$ a dummy okay\'' );
is( $res->output, 'OK: $$ a dummy okay', 'Proves that $$ is not being expanded again' );


my %state = (
	ok => 0,
	warning => 1,
	critical => 2,
	unknown => 3,
	);
foreach my $current_state (qw(ok warning critical unknown)) {
	foreach my $new_state (qw(ok warning critical unknown)) {
		$res = NPTest->testCmd( "./negate --$current_state=$new_state ./check_dummy ".$state{$current_state}." 'Fake $new_state'" );
		is( $res->return_code, $state{$new_state}, "Got fake $new_state" );
		is( $res->output, uc($current_state).": Fake $new_state" );
	}
}

