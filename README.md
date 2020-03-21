# Tracer
This is simply a repository for any **Ray Tracing** code written by me. Currently, it only contains **Swift** code for the *first* and *second* Ray Tracing mini books. I don't know why so many people don't do the third book, maybe they got better resource respecting to the topics of third mini book, the mini book do really ignored alot things when describing the topics.

*256 Ray 1024x1024*  | *256 Ray 512x512*
:---:|:---:
![](Captures/capture_a.PNG) | ![](Captures/capture_b.PNG)


### Why do I use Swift for Ray Tracing?
If I write the same C++ code from the books, I may forgot the concepts and logics very quickly. Using a different language will push my brain to work, and I will remember longer. 


### Bigger things to-do:
- [x] [Ray Tracing: In One Weekend](https://raytracing.github.io/books/RayTracingInOneWeekend.html)
- [x] [Ray Tracing: The Next Week](https://raytracing.github.io/books/RayTracingTheNextWeek.html)
- [ ] [Ray Tracing: The Rest of Your Life](https://raytracing.github.io/books/RayTracingTheRestOfYourLife.html)
- [ ] [Metal API](https://developer.apple.com/documentation/metal)
- [ ] [TU Wien Rendering](https://www.cg.tuwien.ac.at/courses/Rendering/VU.SS2020.html)
- [ ] [Ray Tracing Gems](https://www.realtimerendering.com/raytracinggems/)

### Small things to-do:
- [x] Basic GUI & Menu
- [ ] Pretty GUI 
- [ ] Copy to pasteboard
- [ ] Export as PNG file
- [ ] Cancelable tasks 
- [ ] Camera control

### Problems
- Using too many SIMD commands could block the main thread.
- Sometimes, memory costing is high when launching the app.