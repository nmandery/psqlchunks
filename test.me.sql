-------------------------------------------------------------
-- start: Utility for dropping constrains bug #3766
-- dkgjh ddfg dfg  dfg dg  fg
-------------------------------------------------------------
-- das ist ein test
select 1=1;
-------------------------------------------------------------
-- end: Utiiiiility for dropping constrains  bug #3766
-------------------------------------------------------------

-------------------------------------------------------------
-- start: nothing
-------------------------------------------------------------

-------------------------------------------------------------
-- end: nothing
-------------------------------------------------------------


-------------------------------------------------------------
-- start: Utilidfsdfug #3766
-------------------------------------------------------------
-- this will fail...
select * from ifdigg; --- dffd
-- ... and it did.

-------------------------------------------------------------
-- start: failing casts
-------------------------------------------------------------
select now() = '344';




-------------------------------------------------------------
-- start: stuff 
-------------------------------------------------------------
select * from pg_stat_activity;

