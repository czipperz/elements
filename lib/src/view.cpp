/*=============================================================================
   Copyright (c) 2016-2019 Joel de Guzman

   Distributed under the MIT License [ https://opensource.org/licenses/MIT ]
=============================================================================*/
#include <elements/view.hpp>
#include <elements/window.hpp>
#include <elements/support/context.hpp>

 namespace cycfi { namespace elements
 {
   view::view(host_view h)
    : base_view(h)
    , _work(_io)
   {}

   view::view(window& win)
    : base_view(win.host())
    , _work(_io)
   {
      on_change_limits = [&win](view_limits limits_)
      {
         win.limits(limits_);
      };
   }

   view::~view()
   {
      _io.stop();
   }

   bool view::set_limits()
   {
      if (_content.empty())
         return false;

      auto surface_ = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, nullptr);
      auto context_ = cairo_create(surface_);
      canvas cnv{ *context_ };
      bool resized = false;

      // Update the limits and constrain the window size to the limits
      basic_context bctx{ *this, cnv };
      auto limits_ = _content.limits(bctx);
      if (limits_.min != _current_limits.min || limits_.max != _current_limits.max)
      {
         resized = true;
         _current_limits = limits_;
         if (on_change_limits)
            on_change_limits(limits_);
      }

      cairo_surface_destroy(surface_);
      cairo_destroy(context_);
      return resized;
   }

   void view::draw(cairo_t* context_, rect dirty_)
   {
      if (_content.empty())
         return;

      _dirty = dirty_;

      // Update the limits and constrain the window size to the limits
      if (set_limits())
      {
         refresh();
         return;
      }

      canvas cnv{ *context_ };
      auto size_ = size();
      rect subj_bounds = { 0, 0, size_.x, size_.y };
      context ctx{ *this, cnv, &_content, subj_bounds };

      // layout the subject only if the window bounds changes
      if (_relayout || subj_bounds != _current_bounds)
      {
         _relayout = false;
         _current_bounds = subj_bounds;
         _content.layout(ctx);
      }

      // draw the subject
      _content.draw(ctx);
   }

   namespace
   {
      template <typename F, typename This>
      void call(F f, This& self, rect _current_bounds)
      {
         auto surface_ = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, nullptr);
         auto context_ = cairo_create(surface_);
         canvas cnv{ *context_ };
         context ctx { self, cnv, &self.content(), _current_bounds };

         f(ctx, self.content());

         cairo_surface_destroy(surface_);
         cairo_destroy(context_);
      }
   }

   void view::refresh()
   {
      // Allow refresh to be called from another thread
      _io.post(
         [this]()
         {
            base_view::refresh();
         }
      );
   }

   void view::refresh(rect area)
   {
      // Allow refresh to be called from another thread
      _io.post(
         [this, area]()
         {
            base_view::refresh(area);
         }
      );
   }

   void view::refresh(element& element)
   {
      if (_current_bounds.is_empty())
         return;

      call(
         [&element](auto const& ctx, auto& _content) { _content.refresh(ctx, element); },
         *this, _current_bounds
      );
   }

   void view::refresh(context const& ctx)
   {
      refresh(ctx.bounds);
   }

   void view::click(mouse_button btn)
   {
      _current_button = btn;
      if (_content.empty())
         return;

      call(
         [btn, this](auto const& ctx, auto& _content)
         {
            _content.click(ctx, btn);
            _is_focus = _content.focus();
         },
         *this, _current_bounds
      );
   }

   void view::drag(mouse_button btn)
   {
      _current_button = btn;
      if (_content.empty())
         return;

      call(
         [btn](auto const& ctx, auto& _content) { _content.drag(ctx, btn); },
         *this, _current_bounds
      );
   }

   void view::cursor(point p, cursor_tracking status)
   {
      if (_content.empty())
         return;

      call(
         [p, status](auto const& ctx, auto& _content)
         {
            if (!_content.cursor(ctx, p, status))
               set_cursor(cursor_type::arrow);
         },
         *this, _current_bounds
      );
   }

   void view::scroll(point dir, point p)
   {
      if (_content.empty())
         return;

      call(
         [dir, p](auto const& ctx, auto& _content) { _content.scroll(ctx, dir, p); },
         *this, _current_bounds
      );
   }

   void view::key(key_info const& k)
   {
      if (_content.empty())
         return;

      call(
         [k](auto const& ctx, auto& _content) { _content.key(ctx, k); },
         *this, _current_bounds
      );
   }

   void view::text(text_info const& info)
   {
      if (_content.empty())
         return;

      call(
         [info](auto const& ctx, auto& _content) { _content.text(ctx, info); },
         *this, _current_bounds
      );
   }

   void view::add_undo(undo_redo_task f)
   {
      _undo_stack.push(f);
      if (has_redo())
      {
         // clear the redo stack
         undo_stack_type empty{};
         _redo_stack.swap(empty);
      }
   }

   bool view::undo()
   {
      if (has_undo())
      {
         auto t = _undo_stack.top();
         _undo_stack.pop();
         _redo_stack.push(t);
         t.undo();  // execute undo function
         return true;
      }
      return false;
   }

   bool view::redo()
   {
      if (has_redo())
      {
         auto t = _redo_stack.top();
         _undo_stack.push(t);
         _redo_stack.pop();
         t.redo();  // execute redo function
         return true;
      }
      return false;
   }

   void view::focus(focus_request r)
   {
      if (_content.empty() || !_is_focus)
         return;

      _content.focus(r);
      refresh();
   }

   void view::content(layers_type&& layers)
   {
      _content = std::forward<layers_type>(layers);
      set_limits();
   }

   void view::poll()
   {
      _io.poll();
   }
}}
