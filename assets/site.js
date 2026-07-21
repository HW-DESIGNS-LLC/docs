/* HW Designs — shared site script (assets/site.js)
   Scroll-spy: highlights the nav pill for the section currently on screen.
   Works on any page whose header nav links point at #section-ids.
   Progressive enhancement — the site works fine without it. */
(function () {
  var links = Array.prototype.slice.call(
    document.querySelectorAll('.site-nav a[href^="#"]')
  );
  if (!links.length || !('IntersectionObserver' in window)) return;

  var map = {};
  var sections = [];
  links.forEach(function (a) {
    var id = a.getAttribute('href').slice(1);
    var sec = document.getElementById(id);
    if (sec) { map[id] = a; sections.push(sec); }
  });
  if (!sections.length) return;

  function setActive(id) {
    links.forEach(function (a) { a.classList.remove('active'); });
    if (map[id]) map[id].classList.add('active');
  }

  var current = null;
  var io = new IntersectionObserver(function (entries) {
    entries.forEach(function (e) {
      if (e.isIntersecting) { current = e.target.id; setActive(current); }
    });
  }, { rootMargin: '-40% 0px -55% 0px', threshold: 0 });

  sections.forEach(function (s) { io.observe(s); });

  // Highlight immediately on click, before the scroll settles.
  links.forEach(function (a) {
    a.addEventListener('click', function () {
      setActive(a.getAttribute('href').slice(1));
    });
  });
})();
