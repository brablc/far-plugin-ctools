=head1 COPYRIGHT
/*
 * Copyright (c) Ondrej Brablc, 2001 <brablc@yahoo.com>
 * You can use, modify, distribute this source code  or
 * any other parts of BlockPro plugin only according to
 * BlockPro  License  (see  \Doc\License.txt  for  more
 * information).
 */
=cut

$file =  shift;

unless ($file)
{
    print "Language filename missing!";
    exit;
}

open(IN,  "<${file}Lng.h");
open(LNG, ">${file}.lng");

while (<IN>)
{
    if (s/^.*\/\/\s*(.*\n)$/$1/)
    {
        print LNG $_;
    }
}

close LNG;
close IN;
