/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

package com.facebook.redextest;

import static java.lang.Math.*;
import java.util.ArrayList;

public class PointsToSemantics {
  public interface I { I f(); }

  public class Base implements I {
    public I f() { return new Base(); }
  }

  public class X extends Base {
    public I g() { return super.f(); }
  }

  private static I cast(I o) {
    if (o instanceof X) {
      X x = (X) o;
      return x.g();
    }
    return o.f();
  }

  public static class A {
    ArrayList<String> m_list;

    public A(int n) { m_list = new ArrayList<>(); }

    public A(int n, String s) {
      m_list = new ArrayList<>();
      for (int i = 0; i < n; ++i) {
        m_list.add(s);
      }
    }
  }

  public static class B {
    static String[] strs() {
      String[] s = {"a", "b", "c", "d"};
      return s;
    }

    static int[] ints() {
      int[] i = {1, 2, 3, 4, 5};
      return i;
    }

    String pick(int n) { return strs()[ints()[n]]; }
  }

  public interface Processor { void run(); }

  private static class Time {
    private long m_t;

    Time(long t) { m_t = t; }

    void sleep(long t) {}

    void repeat(long t, Processor p) {
      while (true) {
        p.run();
        sleep(max(t, m_t));
      }
    }
  }

  public static class C {
    C next;
    A val;

    public C(A v, C n) {
      next = n;
      val = v;
    }

    public A nth(int n) {
      C x = this;
      for (int i = 0; i < n; ++i) {
        x = x.next;
      }
      return x.val;
    }
  }

  static A a1 = new A(10);
  static A a2 = new A(5, "something");

  A extract() {
    C c = new C(a1, new C(a2, null));
    return c.nth(2);
  }

  public native int[] nativeMethod();

  static class AnException extends Exception {}

  X[] arrayOfX(int n) throws AnException {
    if (n > 10) {
      throw new AnException();
    }
    return new X[n];
  }

  I runOnArrayOfX(int n) {
    try {
      I[] a = arrayOfX(n);
      for (int i = 0; i < n; ++i) {
        a[i] = cast(a[i]);
      }
      return a[0];
    }
    catch (AnException e) {
      System.out.println(e.getMessage());
      return new Base();
    }
  }

  long[] longMethod(long a, long b, long c, long d, long e, int n, long[] array) {
    array[n] = a + b + c + d + e;
    return array;
  }

  class Complex {
    A a;
    B b;
    Complex c;
    int d;
  }

  int unusedFields(Complex x) {
    Complex y = x.c.c;
    String s = y.b.pick(2);
    return y.c.c.c.c.d;
  }
}
