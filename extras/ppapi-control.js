// Feel free to copy this file into your projects. Public domain.
// The VLC side API is versioned same as on the JS side, so it's possible to use
// two different API versions on the same instance.

VlcInstance = function (element) {
  "use strict";

  if(element === undefined) return undefined;
  if(element === null) return null;

  // --- internal state vars ---
  var next_request_id = 0;
  var inflight_requests = [];

  var events = {};

  // --- internal state vars ---

  var root = this;
  this.location = "";

  this.element = element;

  function define_constant(name, value) {
    Object.defineProperty(root, name, {
      "value": value,
      "writable": false
    });
  }

  // Constants
  define_constant("API_VERSION", 1); // THIS NEEDS TO BE KEPT IN SYNC WITH `ppapi-control.cpp`
  // Constants

  // Event IDs
  this.ON_READY_EVENT = 0;
  // /Event IDs

  function SendAsyncRequest(location, args, callback) {
    var request = {};

    request.type = "request";
    request.subtype = undefined;
    request.request_id = next_request_id++;
    request.location = location;
    request.args = args;
    request.version = root.API_VERSION;

    element.postMessage(request);

    inflight_requests[request.request_id] = callback;
    return request.request_id;
  }
  function SendSyncRequest(location, args, on_error) {
    var request = {};

    request.type = "request";
    request.subtype = undefined;
    request.request_id = next_request_id++;
    request.location = location;
    request.args = args;
    request.version = root.API_VERSION;

    var response = element.postMessageAndAwaitResponse(request);
    if(response.return_code >= 400) {
      if(on_error == null) {
        throw response.return_code;
      } else {
        return on_error(root, location, args);
      }
    }
    return response.return_value;
  }

  function get_base_loc(that) {
    var abs = "";
    for(var parent = that; parent !== root; parent = parent.parent) {
      abs = "/" + parent.location + abs;
    }
    return abs;
  }
  function create_call_async(self) {
    var baseloc = get_base_loc(self);
    return function(loc, arg, cb) {
      return SendAsyncRequest(baseloc + "/" + loc + "()", arg, cb);
    };
  }
  function create_call_sync(self) {
    var baseloc = get_base_loc(self);
    return function(loc, arg, cb) {
      return SendSyncRequest(baseloc + "/" + loc + "()", arg);
    };
  }

  function define_property(self, loc, writable, map) {
    var local_async_send = create_call_async(self);
    var local_sync_send = create_call_sync(self);

    var setter = null;
    if(writable === true) {
      setter = function(value) { return local_sync_send(loc + ".set", value); };
    } else {
      setter = function() { throw new Error("read-only property"); };
    }

    Object.defineProperty(self, loc, {
      __proto__: null,
      get: function() {
        var value = local_sync_send(loc + ".get", undefined);

        if(map !== null && map !== undefined) {
          return map(value);
        } else {
          return value;
        }
      },
      set: setter,
    });
  }

  function subscribe_to_event(event_loc) {
    return SendSyncRequest("/sys/events/subscribe_to_event()", event_loc);
  }
  function unsubscribe_from_event(event_loc) {
    return SendSyncRequest("/sys/events/unsubscribe_from_event()", event_loc);
  }

  function create_add_event_listener(self) {
    var baseloc = get_base_loc(self) + "/event/";
    return function(loc, cb) {
      var fullloc = baseloc + loc + "()";
      subscribe_to_event(fullloc);
      var listeners = events[fullloc];
      if(listeners === undefined) {
        listeners =
          events[fullloc] =
          new Array();
      }
      listeners.push(cb);
    };
  }
  function create_remove_event_listener(self) {
    var baseloc = get_base_loc(self) + "/event/";
    return function(loc, cb) {
      var fullloc = baseloc + loc + "()";
      unsubscribe_from_event(fullloc);
      var listeners = events[fullloc];
      if(listeners === undefined) {
        return false;
      }
      var found = false;
      events[fullloc] = listeners.filter(function(v) {
        if(v === cb) {
          found = true;
          return false;
        } else {
          return true;
        }
      });

      return found;
    };
  }
  function define_event_listener_funs(self) {
    self.addEventListener = create_add_event_listener(self);
    self.removeEventListener = create_remove_event_listener(self);
  }

  function handleMessage(message) {
    if(message.data.type === 'event') {
      var event_substr = '/event/';
      var event_name = message.data.location.slice(  message.data.location.indexOf(event_substr)
                                                   + event_substr.length);
      message.data.getEventName = function() {
        return event_name;
      };
      var loc = message.data.location;
      var cbs = events[loc];
      if(cbs === undefined) { return; }

      cbs.forEach(function(v) {
        v(message);
      })
    } else if(message.data.type === 'return') {
      var callback = inflight_requests[message.data.request_id];
      if(callback === undefined || callback === null) {
        if(message.data.return_code >= 400) {
          console.warn("Request #" + message.data.request_id + " failed: `" + message.data.return_code + "`");
        }
        return;
      }

      var callback_msg = {};

      callback_msg.success = function() {
        return message.data.return_code < 400;
      };
      callback_msg.getReturnValue = function() {
        return message.data.return_value;
      };
      callback_msg.getRequestId = function() {
        return message.data.request_id;
      };

      callback(callback_msg);
      inflight_requests[message.data.request_id] = null;
    } else {
      console.warn("Recieved unknown message type: `" + message.data.type + "`");
    }
  }

  element.addEventListener("message", handleMessage, true);

  function Playlist(parent) {
    this.parent = parent;
    this.location = "playlist";

    var local_async_send = create_call_async(this);
    var local_sync_send = create_call_sync(this);

    this.enqueue = function(items, callback) {
      return local_async_send("enqueue", items, callback);
    };
    this.dequeue = function(items, callback) {
      return local_async_send("dequeue", items, callback);
    };
    this.clear = function(callback) {
      return local_async_send("clear", null, callback);
    }

    define_property(this, "items", false, function(items) {
      return items
        .map(function(item) {
          item.play = function() {
            root.input.item = item.playlist_item_id;
          };
          return item;
        });
    });
    define_property(this, "looping", true);
    define_property(this, "repeating", true);
    define_property(this, "status", false);

    this.STOPPED_STATUS = 0;
    this.RUNNING_STATUS = 1;
    this.PAUSED_STATUS = 2;

    this.isRunning = function() {
      return this.status === this.RUNNING_STATUS;
    };
    this.isPaused = function() {
      return this.status === this.PAUSED_STATUS;
    };
    this.isStopped = function() {
      return this.status === this.STOPPED_STATUS;
    };

    this.next = function(callback) {
      return local_async_send("next", undefined, callback);
    };
    this.prev = function(callback) {
      return local_async_send("prev", undefined, callback);
    };

    this.play = function(callback) {
      return local_async_send("play", undefined, callback);
    };
    this.pause = function(callback) {
      return local_async_send("pause", undefined, callback);
    };
    this.stop = function(callback) {
      return local_async_send("stop", undefined, callback);
    };

    define_event_listener_funs(this);

    function Audio(parent) {
      this.parent = parent;
      this.location = "audio";

      define_property(this, "muted", true);
      define_property(this, "volume", true);

      return this;
    }
    this.audio = new Audio(this);

    return this;
  }
  this.playlist = new Playlist(this);

  function Input(parent) {
    this.parent = parent;
    this.location = "input";

    var local_async_send = create_call_async(this);

    define_property(this, "position", true);
    define_property(this, "time", true);

    define_property(this, "rate", true);
    define_property(this, "length", false);

    define_property(this, "state", true);
    define_property(this, "item", true);

    function Video(parent) {
      this.parent = parent;
      this.location = "video";

      var local_async_send = create_call_async(this);

      this.nextFrame = function(callback) {
        return local_async_send("next-frame", undefined, callback);
      };
      this.prevFrame = function(callback) {
        return local_async_send("prev-frame", undefined, callback);
      };

      return this;
    }

    this.video = new Video(this);

    this.restart_es = function(which, callback) {
      return local_async_send("restart_es", which, callback);
    };

    define_event_listener_funs(this);

    return this;
  }
  this.input = new Input(this);


  function Sys(parent) {
    this.parent = parent;
    this.location = "sys";

    define_property(this, "log_level", true);
    define_property(this, "version", false);

    var local_async_send = create_call_async(this);

    this.purge_cache = function(callback) {
      return local_async_send("purge_cache", undefined, callback);
    };

    return this;
  }
  this.sys = new Sys(this);

  // NOTE this is unsupported and may be broken by changes to ppapi-control
  // at any time.
  this.callCustomMethod = function(location, version, args, callback) {
    var request = {};

    request.type = "request";
    request.subtype = undefined;
    request.request_id = next_request_id++;
    request.location = location;
    request.args = args;
    request.version = version;

    if(vlc_version === undefined) {
      initial_request_queue.push(request);
    } else {
      element.postMessage(request);
    }

    inflight_requests[request.request_id] = callback;
    return request.request_id;
  };

  return this;
}
