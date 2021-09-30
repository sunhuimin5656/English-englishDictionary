#!/usr/bin/perl -Tw

use strict;
use CGI;

my($cgi) = new CGI;

print $cgi->header;
my($word) = "a";
my($explain) = "afer a";
$word = $cgi->param('word') if defined $cgi->param('word');
$explain = $cgi->param('explain') if defined $cgi->param('explain');

print $cgi->start_html(-title => "BLUE",
                       -BGCOLOR => "blue");
print $cgi->h1("$word");
print $cgi->h1("$explain");
print $cgi->end_html;
