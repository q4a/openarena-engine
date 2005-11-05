<p>Quake 3 source code was released on August 20th by the fine folks at id Software.
We thank them for that, and are continuing the mission of producing a Quake 3 that is
without fault.</p>
<img src="images/thenameofthisprojectis3.jpg" class="right" alt="Logo" />

<h2>What's the point?</h2>
<p>This project aims to build upon id Software's Quake 3 source code release. The source
code was released on August 20, 2005 under the
<a href="http://www.gnu.org/copyleft/gpl.html" title="General Public License">GPL</a>.
Since then, we have been feverishly cleaning up, fixing bugs, and adding features. Our
eventual goal is to have created <em>the</em> Quake 3 source code distribution upon which
people base their games, ports, and mods. Our focus initially is to get the game working
with our updates on Mac OS X, Windows, and Linux. Sane new features are also welcome.
Modern graphical upgrades (ie. bloom lighting) would have to be disabled by default.
</p>

<h2>Progress</h2>
<p>While a lot is left to be done, quite a few goals have been met already. Quake 3
now works native on x86_64 and PowerPC architectures, plus the game builds and runs
on <a href="http://www.freebsd.org/">FreeBSD</a>.
<a href="http://libsdl.org" title="Simple DirectMedia Layer">SDL</a> is now used for input,
OpenGL context management, and sound, making the game a lot easier to port to new platforms
and architectures than it was before. Security holes and other problems have been repaired.
A more in-depth database of new features and working status on different platforms is
on the <a href="?page=status">Status</a> section.</p>

<h2>The future</h2>
<p>Current goals for 1.34 (<acronym title="Subversion">SVN</acronym> is 1.33, last id
build was 1.32) include:</p>
<ul>
	<li>OpenAL support</li>
	<li>Binaries for Windows XP and Mac OS X at release. Linux will just get a source
	package unless somebody feels inclined to set up a new installer.</li>
	<li>Removal of DirectX for <acronym title="Microsoft Visual C++">MSVC</acronym>
	and <a href="http://mingw.org" title="Minimalist GNU for Windows">MinGW</a> builds.</li>
</ul>