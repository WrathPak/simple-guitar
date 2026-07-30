// @types/react 18 doesn't type the standard HTML `inert` global attribute
// yet (it lands with React 19's types). React DOM 18.3+ still renders it
// correctly at runtime -- it just needs a string ("" to enable, undefined
// to omit), not a JS boolean, since it isn't in React's built-in list of
// boolean-valued attributes. Used by Room.tsx to make the blurred
// background scene un-tabbable (and hidden from assistive tech) while a
// device is camera-focused, matching its visual blur(9px)/dim(45%) state.
import "react";

declare module "react" {
  interface HTMLAttributes<T> {
    inert?: string;
  }
}
