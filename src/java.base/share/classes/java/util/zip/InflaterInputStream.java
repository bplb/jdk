/*
 * Copyright (c) 1996, 2025, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package java.util.zip;

import java.io.FilterInputStream;
import java.io.InputStream;
import java.io.IOException;
import java.io.EOFException;
import java.util.Objects;

/**
 * This class implements a stream filter for uncompressing data in the
 * "deflate" compression format. It is also used as the basis for other
 * decompression filters, such as GZIPInputStream.
 * <p> Unless otherwise noted, passing a {@code null} argument to a constructor
 * or method in this class will cause a {@link NullPointerException} to be
 * thrown.
 *
 * <h2 id="decompressor-usage">Decompressor Usage</h2>
 * An {@code InflaterInputStream} created without
 * specifying a {@linkplain Inflater decompressor} will create a decompressor
 * at construction time, and close the decompressor when the input stream
 * is {@linkplain #close closed}.
 * <p>
 * If a decompressor is specified when creating a {@code InflaterInputStream}, it is the
 * responsibility of the caller to {@linkplain Inflater#close close} the
 * decompressor after closing the input stream.
 *
 * @apiNote
 * The {@link #close} method should be called to release resources used by this
 * stream, either directly, or with the {@code try}-with-resources statement.
 *
 * @see         Inflater
 * @author      David Connelly
 * @since 1.1
 */
public class InflaterInputStream extends FilterInputStream {
    /**
     * Decompressor for this stream.
     */
    protected Inflater inf;

    /**
     * Input buffer for decompression.
     */
    protected byte[] buf;

    /**
     * The total number of bytes read into the input buffer.
     */
    protected int len;

    private boolean closed = false;
    // this flag is set to true after EOF has reached
    private boolean reachEOF = false;

    /**
     * Check to make sure that this stream has not been closed
     */
    private void ensureOpen() throws IOException {
        if (closed) {
            throw new IOException("Stream closed");
        }
    }


    /**
     * Creates a new input stream with the specified decompressor and
     * buffer size.
     * <p>
     * {@linkplain #close() Closing} this input stream
     * {@linkplain ##decompressor-usage will not close} the given
     * {@linkplain Inflater decompressor}.
     *
     * @param in the input stream
     * @param inf the decompressor ("inflater")
     * @param size the input buffer size
     * @throws    IllegalArgumentException if {@code size <= 0}
     */
    public InflaterInputStream(InputStream in, Inflater inf, int size) {
        super(in);
        if (in == null || inf == null) {
            throw new NullPointerException();
        } else if (size <= 0) {
            throw new IllegalArgumentException("buffer size <= 0");
        }
        this.inf = inf;
        buf = new byte[size];
    }

    /**
     * Creates a new input stream with the specified decompressor and a
     * default buffer size.
     * <p>
     * {@linkplain #close() Closing} this input stream
     * {@linkplain ##decompressor-usage will not close} the given
     * {@linkplain Inflater decompressor}.
     *
     * @param in the input stream
     * @param inf the decompressor ("inflater")
     */
    public InflaterInputStream(InputStream in, Inflater inf) {
        this(in, inf, 512);
    }

    boolean usesDefaultInflater = false;

    /**
     * Creates a new input stream and decompressor with a
     * default buffer size.
     * <p>
     * The decompressor will be closed when this input stream
     * is {@linkplain #close() closed}.
     *
     * @param in the input stream
     */
    public InflaterInputStream(InputStream in) {
        this(in, in != null ? new Inflater() : null);
        usesDefaultInflater = true;
    }

    private final byte[] singleByteBuf = new byte[1];

    /**
     * Reads a byte of uncompressed data. This method will block until
     * enough input is available for decompression.
     * @return the byte read, or -1 if end of compressed input is reached
     * @throws    IOException if an I/O error has occurred
     */
    @Override
    public int read() throws IOException {
        ensureOpen();
        return read(singleByteBuf, 0, 1) == -1 ? -1 : Byte.toUnsignedInt(singleByteBuf[0]);
    }

    /**
     * Reads uncompressed data into an array of bytes, returning the number of inflated
     * bytes. If {@code len} is not zero, the method will block until some input can be
     * decompressed; otherwise, no bytes are read and {@code 0} is returned.
     * <p>
     * If this method returns a nonzero integer <i>n</i> then {@code buf[off]}
     * through {@code buf[off+}<i>n</i>{@code -1]} contain the uncompressed
     * data.  The content of elements {@code buf[off+}<i>n</i>{@code ]} through
     * {@code buf[off+}<i>len</i>{@code -1]} is undefined, contrary to the
     * specification of the {@link java.io.InputStream InputStream} superclass,
     * so an implementation is free to modify these elements during the inflate
     * operation. If this method returns {@code -1} or throws an exception then
     * the content of {@code buf[off]} through {@code buf[off+}<i>len</i>{@code
     * -1]} is undefined.
     *
     * @param b the buffer into which the data is read
     * @param off the start offset in the destination array {@code b}
     * @param len the maximum number of bytes read
     * @return the actual number of bytes inflated, or -1 if the end of the
     *         compressed input is reached or a preset dictionary is needed
     * @throws     IndexOutOfBoundsException If {@code off} is negative,
     * {@code len} is negative, or {@code len} is greater than
     * {@code b.length - off}
     * @throws    ZipException if a ZIP format error has occurred
     * @throws    IOException if an I/O error has occurred
     */
    @Override
    public int read(byte[] b, int off, int len) throws IOException {
        ensureOpen();
        if (b == null) {
            throw new NullPointerException();
        }
        Objects.checkFromIndexSize(off, len, b.length);
        if (len == 0) {
            return 0;
        }
        try {
            int n;
            do {
                if (inf.finished() || inf.needsDictionary()) {
                    reachEOF = true;
                    return -1;
                }
                if (inf.needsInput() && !inf.hasPendingOutput()) {
                    // Even if needsInput() is true, the native inflater may have some
                    // buffered data which couldn't fit in to the output buffer during the
                    // last call to inflate. Consume that buffered data first before calling
                    // fill() to avoid an EOF error if no more input is available and the
                    // next call to inflate will finish the inflation.
                    fill();
                }
            } while ((n = inf.inflate(b, off, len)) == 0);
            return n;
        } catch (DataFormatException e) {
            String s = e.getMessage();
            throw new ZipException(s != null ? s : "Invalid ZLIB data format");
        }
    }

    /**
     * Returns 0 after EOF has been reached, otherwise always return 1.
     * <p>
     * Programs should not count on this method to return the actual number
     * of bytes that could be read without blocking.
     *
     * @return     1 before EOF and 0 after EOF.
     * @throws     IOException  if an I/O error occurs.
     *
     */
    @Override
    public int available() throws IOException {
        ensureOpen();
        if (reachEOF) {
            return 0;
        } else if (inf.finished()) {
            // the end of the compressed data stream has been reached
            reachEOF = true;
            return 0;
        } else {
            return 1;
        }
    }

    /**
     * Skips specified number of bytes of uncompressed data.
     * This method may block until the specified number of bytes are skipped
     * or end of stream is reached.
     *
     * @implNote
     * This method skips at most {@code Integer.MAX_VALUE} bytes.
     *
     * @param n the number of bytes to skip. If {@code n} is zero then no bytes are skipped.
     * @return the actual number of bytes skipped, which might be zero
     * @throws IOException if an I/O error occurs or if this stream is
     *                     already closed
     * @throws    IllegalArgumentException if {@code n < 0}
     */
    @Override
    public long skip(long n) throws IOException {
        if (n < 0) {
            throw new IllegalArgumentException("negative skip length");
        }
        ensureOpen();
        int max = (int)Math.min(n, Integer.MAX_VALUE);
        int total = 0;
        byte[] b = new byte[Math.min(max, 512)];
        while (total < max) {
            int len = max - total;
            if (len > b.length) {
                len = b.length;
            }
            len = read(b, 0, len);
            if (len == -1) {
                reachEOF = true;
                break;
            }
            total += len;
        }
        return total;
    }

    /**
     * Closes this input stream and releases any system resources associated
     * with the stream.
     *
     * @throws    IOException if an I/O error has occurred
     */
    @Override
    public void close() throws IOException {
        if (!closed) {
            if (usesDefaultInflater)
                inf.end();
            in.close();
            closed = true;
        }
    }

    /**
     * Fills input buffer with more data to decompress.
     * @implSpec
     * This method will read up to {@link #buf}.length bytes into the input
     * buffer, {@link #buf}, starting at element {@code 0}. The {@link #len}
     * field will be set to the number of bytes read.
     * @throws    IOException if an I/O error has occurred
     * @throws    EOFException if the end of input stream has been reached
     *            unexpectedly
     */
    protected void fill() throws IOException {
        ensureOpen();
        len = in.read(buf, 0, buf.length);
        if (len == -1) {
            throw new EOFException("Unexpected end of ZLIB input stream");
        }
        inf.setInput(buf, 0, len);
    }

    /**
     * Tests if this input stream supports the {@code mark} and
     * {@code reset} methods. The {@code markSupported}
     * method of {@code InflaterInputStream} returns
     * {@code false}.
     *
     * @return  a {@code boolean} indicating if this stream type supports
     *          the {@code mark} and {@code reset} methods.
     * @see     java.io.InputStream#mark(int)
     * @see     java.io.InputStream#reset()
     */
    @Override
    public boolean markSupported() {
        return false;
    }

    /**
     * Marks the current position in this input stream.
     *
     * @implSpec The {@code mark} method of {@code InflaterInputStream}
     * does nothing.
     *
     * @param   readlimit   the maximum limit of bytes that can be read before
     *                      the mark position becomes invalid.
     * @see     java.io.InputStream#reset()
     */
    @Override
    public void mark(int readlimit) {
    }

    /**
     * Repositions this stream to the position at the time the
     * {@code mark} method was last called on this input stream.
     *
     * @implSpec The method {@code reset} for class
     * {@code InflaterInputStream} does nothing except throw an
     * {@code IOException}.
     *
     * @throws     IOException  if this method is invoked.
     * @see     java.io.InputStream#mark(int)
     * @see     java.io.IOException
     */
    @Override
    public void reset() throws IOException {
        throw new IOException("mark/reset not supported");
    }
}
